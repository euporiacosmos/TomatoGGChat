#include "pch.h"
#include "WebViewHost.h"
#include "TomatoScripts.h"
#include "Util.h"
#include "json.hpp"

using namespace Microsoft::WRL;

namespace {
// ExecuteScript 가 문자열을 반환하면 JSON 문자열 리터럴("...")로 오므로 풀어준다.
std::wstring UnwrapJsonString(const std::wstring& json)
{
    try {
        auto j = nlohmann::json::parse(ToUtf8(json));
        if (j.is_string()) return FromUtf8(j.get<std::string>());
    } catch (...) {}
    return L"";
}
}

void CWebViewHost::Init(HWND hwnd, const RECT& rc, const std::wstring& startUrl,
                        std::function<void(bool)> onReady)
{
    // exe 폴더가 쓰기 불가일 수 있으므로 사용자 데이터 폴더는 LOCALAPPDATA 아래에 둔다.
    wchar_t base[MAX_PATH] = {};
    GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    std::wstring udf = std::wstring(base) + L"\\TomatoGGChat\\WebView2";

    auto alive = m_alive;
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, udf.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [=](HRESULT hrEnv, ICoreWebView2Environment* env) -> HRESULT {
            if (!*alive) return S_OK;
            if (FAILED(hrEnv) || !env) { if (onReady) onReady(false); return S_OK; }

            env->CreateCoreWebView2Controller(hwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [=](HRESULT hrCtrl, ICoreWebView2Controller* ctrl) -> HRESULT {
                    if (!*alive) return S_OK;
                    if (FAILED(hrCtrl) || !ctrl) { if (onReady) onReady(false); return S_OK; }

                    m_ctrl = ctrl;
                    m_ctrl->get_CoreWebView2(&m_wv);
                    m_ctrl->put_Bounds(rc);

                    // 클릭으로 웹뷰가 포커스를 얻으면 휠 대상 = 웹뷰, MFC 쪽을 클릭하면 포커스를 잃는다.
                    m_ctrl->add_GotFocus(
                        Callback<ICoreWebView2FocusChangedEventHandler>(
                        [this, alive](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                            if (*alive && OnFocusChanged) OnFocusChanged(true);
                            return S_OK;
                        }).Get(), &m_gotFocusTok);
                    m_ctrl->add_LostFocus(
                        Callback<ICoreWebView2FocusChangedEventHandler>(
                        [this, alive](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                            if (*alive && OnFocusChanged) OnFocusChanged(false);
                            return S_OK;
                        }).Get(), &m_lostFocusTok);

                    ComPtr<ICoreWebView2Settings> settings;
                    if (SUCCEEDED(m_wv->get_Settings(&settings)) && settings) {
                        settings->put_IsStatusBarEnabled(FALSE);
                        settings->put_AreDevToolsEnabled(TRUE);
                    }

                    // SPA 는 history.pushState 로 URL 만 바뀌는 경우가 많아 SourceChanged 를 사용
                    m_wv->add_SourceChanged(
                        Callback<ICoreWebView2SourceChangedEventHandler>(
                        [this, alive](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                            if (!*alive) return S_OK;
                            LPWSTR uri = nullptr;
                            if (SUCCEEDED(sender->get_Source(&uri)) && uri) {
                                if (OnUrlChanged) OnUrlChanged(uri);
                                CoTaskMemFree(uri);
                            }
                            return S_OK;
                        }).Get(), &m_srcTok);

                    // target=_blank / 광고 팝업을 새 창으로 띄우지 않고 같은 웹뷰에서 연다.
                    m_wv->add_NewWindowRequested(
                        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                        [this, alive](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                            if (!*alive) return S_OK;
                            args->put_Handled(TRUE);
                            LPWSTR uri = nullptr;
                            if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                                if (m_wv) m_wv->Navigate(uri);
                                CoTaskMemFree(uri);
                            }
                            return S_OK;
                        }).Get(), &m_newWinTok);

                    m_wv->Navigate(startUrl.c_str());
                    if (onReady) onReady(true);
                    return S_OK;
                }).Get());
            return S_OK;
        }).Get());

    if (FAILED(hr) && onReady) onReady(false);
}

void CWebViewHost::Close()
{
    *m_alive = false;
    OnUrlChanged = nullptr;
    OnFocusChanged = nullptr;
    if (m_ctrl) {
        m_ctrl->remove_GotFocus(m_gotFocusTok);
        m_ctrl->remove_LostFocus(m_lostFocusTok);
    }
    if (m_wv) {
        m_wv->remove_SourceChanged(m_srcTok);
        m_wv->remove_NewWindowRequested(m_newWinTok);
    }
    if (m_ctrl) m_ctrl->Close();
    m_wv.Reset();
    m_ctrl.Reset();
}

void CWebViewHost::Resize(const RECT& rc) { if (m_ctrl) m_ctrl->put_Bounds(rc); }
void CWebViewHost::Navigate(const std::wstring& url) { if (m_wv) m_wv->Navigate(url.c_str()); }
void CWebViewHost::OpenDevTools() { if (m_wv) m_wv->OpenDevToolsWindow(); }

void CWebViewHost::RunScript(const std::wstring& js, std::function<void(HRESULT, std::wstring)> cb)
{
    if (!m_wv) { if (cb) cb(E_FAIL, L""); return; }
    auto alive = m_alive;
    m_wv->ExecuteScript(js.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [alive, cb](HRESULT hr, LPCWSTR result) -> HRESULT {
            if (!*alive) return S_OK;
            if (cb) cb(hr, (SUCCEEDED(hr) && result) ? std::wstring(result) : std::wstring());
            return S_OK;
        }).Get());
}

void CWebViewHost::GetPageText(std::function<void(std::wstring)> cb)
{
    RunScript(TomatoJs::PageText, [cb](HRESULT hr, std::wstring json) {
        cb(SUCCEEDED(hr) ? UnwrapJsonString(json) : L"");
    });
}

void CWebViewHost::ReadBattles(std::function<void(std::string)> cb)
{
    RunScript(TomatoJs::ReadBattles(), [cb](HRESULT hr, std::wstring json) {
        cb(SUCCEEDED(hr) ? ToUtf8(json) : std::string());
    });
}

void CWebViewHost::ReadSiteMode(std::function<void(std::wstring)> cb)
{
    RunScript(TomatoJs::SiteMode, [cb](HRESULT hr, std::wstring json) {
        cb(SUCCEEDED(hr) ? UnwrapJsonString(json) : L"");
    });
}

void CWebViewHost::ScrollBy(int dyPixels)
{
    RunScript(L"window.scrollBy(0," + std::to_wstring(dyPixels) + L");", nullptr);
}

void CWebViewHost::NudgeBattlesIntoView()
{
    RunScript(TomatoJs::NudgeBattles(), nullptr);
}
