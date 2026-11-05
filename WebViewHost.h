#pragma once
#include <functional>
#include <memory>
#include <string>
#include <wrl.h>
#include <WebView2.h>
#include "Models.h"

// WebView2 래퍼. 반드시 UI(STA) 스레드에서만 사용하세요.
class CWebViewHost
{
public:
    // 주소가 바뀔 때마다 호출 (SPA 의 pushState 포함)
    std::function<void(const std::wstring&)> OnUrlChanged;
    // 웹뷰가 키보드 포커스를 얻으면 true, 잃으면 false (휠 스크롤 대상 판정용)
    std::function<void(bool focused)> OnFocusChanged;

    void Init(HWND hwndParent, const RECT& rc, const std::wstring& startUrl,
              std::function<void(bool ok)> onReady);
    void Close();
    bool IsReady() const { return m_wv.Get() != nullptr; }
    void Resize(const RECT& rc);
    void Navigate(const std::wstring& url);
    void OpenDevTools();

    void GetPageText(std::function<void(std::wstring)> cb);
    void ReadBattles(std::function<void(std::string jsonUtf8)> cb);
    void ReadSiteMode(std::function<void(std::wstring mode)> cb);
    void NudgeBattlesIntoView();
    void ScrollBy(int dyPixels);           // window.scrollBy(0, dy) : 마우스 휠을 웹 페이지로 전달

private:
    void RunScript(const std::wstring& js, std::function<void(HRESULT, std::wstring)> cb);

    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_ctrl;
    Microsoft::WRL::ComPtr<ICoreWebView2>           m_wv;
    EventRegistrationToken m_srcTok{};
    EventRegistrationToken m_newWinTok{};
    EventRegistrationToken m_gotFocusTok{};
    EventRegistrationToken m_lostFocusTok{};
    std::shared_ptr<bool>  m_alive = std::make_shared<bool>(true);   // 콜백 수명 보호
};
