#include "pch.h"
#include "TomatoGGChat.h"
#include "TomatoGGChatDlg.h"
#include "afxdialogex.h"
#include "Message.h"
#include "Util.h"
#include "TomatoParser.h"
#include "PromptBuilder.h"
#include <format>

BEGIN_MESSAGE_MAP(CTomatoGGChatDlg, CDialogEx)
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_WM_LBUTTONDOWN()
    ON_EN_SETFOCUS(IDC_EDIT_INPUT, &CTomatoGGChatDlg::OnEnSetfocusEditInput)
    ON_BN_CLICKED(IDC_BTN_SEND, &CTomatoGGChatDlg::OnBnClickedSend)
    ON_BN_CLICKED(IDC_BTN_REFRESH, &CTomatoGGChatDlg::OnBnClickedRefresh)
    ON_MESSAGE(WM_LLM_LOADED, &CTomatoGGChatDlg::OnLlmLoaded)
    ON_MESSAGE(WM_LLM_TOKEN, &CTomatoGGChatDlg::OnLlmToken)
    ON_MESSAGE(WM_LLM_DONE, &CTomatoGGChatDlg::OnLlmDone)
    ON_MESSAGE(WM_LLM_ERROR, &CTomatoGGChatDlg::OnLlmError)
END_MESSAGE_MAP()

CTomatoGGChatDlg::CTomatoGGChatDlg(CWnd* pParent)
    : CDialogEx(IDD_TOMATOGGCHAT_DIALOG, pParent)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CTomatoGGChatDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_EDIT_INPUT, m_edit);
    DDX_Control(pDX, IDC_BTN_SEND, m_btnSend);
    DDX_Control(pDX, IDC_BTN_REFRESH, m_btnRefresh);
    DDX_Control(pDX, IDC_STATIC_STATUS, m_status);
}

// ═════════════════════════════════════════════════════════════
//  초기화 / 종료
// ═════════════════════════════════════════════════════════════
BOOL CTomatoGGChatDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);
    SetWindowText(L"TomatoGGChat");
    ModifyStyle(0, WS_CLIPCHILDREN);

    m_cfg = AppConfig::Load();

    // 입력창: 오른쪽 아래에 겹쳐진 전송 버튼 밑으로 글자가 들어가지 않게 오른쪽 여백을 둔다.
    m_edit.SetLimitText(1000);
    m_edit.SetMargins(4, kSendW + 10);

    // 디자인용 자리표시자는 숨긴다 (좌표는 코드가 계산)
    if (CWnd* p = GetDlgItem(IDC_WEBVIEW)) p->ShowWindow(SW_HIDE);

    // 채팅창: 코드로 생성. 클릭하면 휠 대상이 채팅으로 바뀐다.
    m_chat.CreateView(this, kChatViewId, CRect(0, 0, 100, 100));
    m_chat.OnClicked = [this] { m_wheelTarget = WheelTarget::Chat; };

    SetWindowPos(nullptr, 0, 0, 1280, 900, SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();
    m_layoutReady = true;
    LayoutChildren();

    AppendSystem(L"TomatoGGChat 시작. Tomato.gg 에서 플레이어를 검색하세요.");
    AppendSystem(L"입력창은 플레이어의 Main 탭에서만 활성화됩니다. (/help 로 명령어 보기)");

    // ── WebView2 생성: 숨겨둔 IDC_WEBVIEW_HOST 자리(m_rcWeb)에 붙는다 ──
    m_web.OnUrlChanged = [this](const std::wstring& url) { OnTomatoUrl(url); };
    m_web.OnFocusChanged = [this](bool focused) {
        m_wheelTarget = focused ? WheelTarget::Web : WheelTarget::Chat;   // 웹뷰 클릭 → 웹 스크롤, 그 외 → 채팅 스크롤
    };
    m_web.Init(GetSafeHwnd(), m_rcWeb, m_cfg.startUrl, [this](bool ok) {
        if (!ok) {
            AppendSystem(L"WebView2 초기화에 실패했습니다. Microsoft Edge WebView2 Runtime 이 설치되어 있는지 확인하세요.");
            return;
        }
        m_webReady = true;
        m_web.Resize(m_rcWeb);
        UpdateUi();
        });

    // ── 모델은 별도 스레드에서 로딩 (수 초 소요) ──
    LlmParams lp;
    lp.modelPath = m_cfg.modelPath;
    lp.nCtx = m_cfg.nCtx;
    lp.nThreads = m_cfg.nThreads;
    lp.temp = m_cfg.temperature;
    lp.topK = m_cfg.topK;
    lp.topP = m_cfg.topP;
    lp.useGpu = m_cfg.useGpu;
    lp.nGpuLayers = m_cfg.gpuLayers;
    lp.gpuDevice = m_cfg.gpuDevice;
    AppendSystem(L"모델 로딩 중...");
    m_llm.LoadAsync(lp, GetSafeHwnd());

    UpdateUi();
    return TRUE;
}

void CTomatoGGChatDlg::OnDestroy()
{
    KillTimer(kTimerPoll);
    KillTimer(kTimerModeWatch);
    ++m_scrapeGen;
    m_llm.Shutdown();      // 생성/로딩 스레드 join
    m_web.Close();
    CDialogEx::OnDestroy();
}

// ═════════════════════════════════════════════════════════════
//  레이아웃
//   · 가로:세로 < 4:3  → 위 웹뷰 / 가운데 채팅 / 아래 입력창
//   · 가로:세로 ≥ 4:3  → 왼쪽 웹뷰 / 오른쪽(위 채팅, 아래 입력창)
// ═════════════════════════════════════════════════════════════
void CTomatoGGChatDlg::LayoutChildren()
{
    if (!m_layoutReady) return;
    CRect rc;
    GetClientRect(&rc);
    if (rc.Width() < 100 || rc.Height() < 100) return;

    const int M = 8, G = 6, BAR = 26;
    auto move = [&](UINT id, const CRect& r) {
        if (CWnd* p = GetDlgItem(id)) p->MoveWindow(r);
        };

    // 상단 바: 상태 텍스트(왼쪽, 남는 폭 전부) + [새로고침](오른쪽 끝)
    const int BTN_W = 90;
    const int barLeft = (int)rc.left + M;
    const int barRight = (int)rc.right - M;
    int y = (int)rc.top + M;

    const int btnLeft = barRight - BTN_W;
    int statusRight = btnLeft - 10;                       // 텍스트와 버튼 사이 간격
    if (statusRight < barLeft + 10) statusRight = barLeft + 10;

    move(IDC_BTN_REFRESH, CRect(btnLeft, y, barRight, y + BAR));
    move(IDC_STATIC_STATUS, CRect(barLeft, y + 5, statusRight, y + BAR));
    y += BAR + G;

    const CRect body(rc.left + M, y, rc.right - M, rc.bottom - M);
    m_wideLayout = (rc.Width() * 3 >= rc.Height() * 4);        // 가로:세로 ≥ 4:3

    CRect web, chat, input;
    if (m_wideLayout) {
        const int webW = (std::min)((int)(body.Width() * m_cfg.webRatio), (int)body.Width() - 320);
        web = CRect(body.left, body.top, body.left + webW, body.bottom);
        const CRect side(web.right + G, body.top, body.right, body.bottom);
        input = CRect(side.left, side.bottom - kInputH, side.right, side.bottom);
        chat = CRect(side.left, side.top, side.right, input.top - G);
    }
    else {
        const int availH = body.Height() - kInputH - 2 * G;
        const int webH = (std::min)((int)(availH * m_cfg.webRatio), availH - 120);
        web = CRect(body.left, body.top, body.right, body.top + webH);
        chat = CRect(body.left, web.bottom + G, body.right, body.bottom - kInputH - G);
        input = CRect(body.left, body.bottom - kInputH, body.right, body.bottom);
    }

    m_rcWeb = web;
    move(IDC_WEBVIEW, web);                               // 숨겨둔 자리표시자
    if (m_chat.m_hWnd) m_chat.SetWindowPos(nullptr, chat.left, chat.top, chat.Width(), chat.Height(),
        SWP_NOZORDER | SWP_NOACTIVATE);
    move(IDC_EDIT_INPUT, input);

    // 전송 버튼: 입력창 오른쪽 아래 구석에 겹쳐 놓는다 (Z 순서 최상단)
    m_btnSend.SetWindowPos(&CWnd::wndTop, input.right - kSendW - 6, input.bottom - kSendH - 6, kSendW, kSendH,
        SWP_NOACTIVATE);

    if (m_web.IsReady()) m_web.Resize(m_rcWeb);
}

void CTomatoGGChatDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialogEx::OnSize(nType, cx, cy);
    if (nType != SIZE_MINIMIZED) LayoutChildren();
}

void CTomatoGGChatDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
    lpMMI->ptMinTrackSize.x = 640;
    lpMMI->ptMinTrackSize.y = 560;
    CDialogEx::OnGetMinMaxInfo(lpMMI);
}

// ═════════════════════════════════════════════════════════════
//  마우스/키 입력
//   · 휠: 마지막으로 클릭한 쪽(웹뷰 or MFC)으로 간다.
//   · Enter = 전송, Shift/Ctrl+Enter = 줄바꿈, Esc/Enter 로 대화상자가 닫히지 않게 막는다.
//   · 웹뷰 안의 키 입력은 건드리지 않고 그대로 통과시킨다.
// ═════════════════════════════════════════════════════════════
bool CTomatoGGChatDlg::IsWebViewWindow(HWND h) const
{
    if (!h || h == m_hWnd || !::IsChild(m_hWnd, h)) return false;
    HWND top = h;
    while (top && ::GetParent(top) != m_hWnd) top = ::GetParent(top);
    if (!top) return false;
    // 우리가 만든/연결한 MFC 컨트롤이 아니면 WebView2 의 윈도우로 본다.
    return CWnd::FromHandlePermanent(top) == nullptr;
}

void CTomatoGGChatDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
    m_wheelTarget = WheelTarget::Chat;       // MFC 창(빈 영역 포함)을 클릭 → 이후 휠은 채팅 스크롤
    SetFocus();                              // 웹뷰의 포커스를 거둔다
    CDialogEx::OnLButtonDown(nFlags, point);
}

void CTomatoGGChatDlg::OnEnSetfocusEditInput()
{
    m_wheelTarget = WheelTarget::Chat;
}

BOOL CTomatoGGChatDlg::PreTranslateMessage(MSG* pMsg)
{
    const bool ours = (pMsg->hwnd == m_hWnd) || ::IsChild(m_hWnd, pMsg->hwnd);
    const bool web = IsWebViewWindow(pMsg->hwnd);

    if (pMsg->message == WM_MOUSEWHEEL && ours) {
        if (pMsg->hwnd == m_edit.m_hWnd) return FALSE;             // 입력창 위에서는 입력창이 스스로 스크롤
        const short delta = GET_WHEEL_DELTA_WPARAM(pMsg->wParam);
        if (m_wheelTarget == WheelTarget::Chat) {
            m_chat.ScrollByWheel(delta);
            return TRUE;
        }
        if (!web) {                                                // 웹뷰가 대상인데 메시지가 MFC 창으로 왔다 → 웹으로 전달
            m_web.ScrollBy(-delta * 100 / WHEEL_DELTA);
            return TRUE;
        }
        return FALSE;                                              // 웹뷰 창으로 온 휠은 웹뷰가 처리
    }

    if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST) {
        if (web) return FALSE;                                     // 웹뷰: IsDialogMessage 를 거치지 않고 그대로 전달
        if (pMsg->message == WM_KEYDOWN) {
            if (pMsg->wParam == VK_RETURN) {
                if (pMsg->hwnd == m_edit.m_hWnd) {
                    if (::GetKeyState(VK_SHIFT) < 0 || ::GetKeyState(VK_CONTROL) < 0) m_edit.ReplaceSel(L"\r\n", TRUE);
                    else SubmitInput();
                }
                return TRUE;
            }
            if (pMsg->wParam == VK_ESCAPE) return TRUE;
        }
    }
    return CDialogEx::PreTranslateMessage(pMsg);
}

// ═════════════════════════════════════════════════════════════
//  URL 변화 → 탭 판정 → 수집 시작/중단
// ═════════════════════════════════════════════════════════════
void CTomatoGGChatDlg::OnTomatoUrl(const std::wstring& url)
{
    const UrlInfo u = CWnxParser::ClassifyUrl(url);
    const bool wasMain = m_onMain;
    m_url = u;
    m_onMain = (u.tab == TabKind::Main);

    if (!m_onMain) {                               // Main 이 아님 → 수집/생성 중단, 입력창 비활성
        if (wasMain) { StopScrape(); CancelGeneration(); }
        UpdateUi();
        return;
    }

    const std::wstring key = u.server + L"|" + u.accountId;
    if (key != m_dataKey) {                        // 새 플레이어
        ResetPlayer(u, key);
        StartScrape();
    }
    else if (!wasMain && !m_data.scrapeComplete) {   // 같은 플레이어의 Main 으로 복귀, 수집이 덜 끝남
        StartScrape();
    }
    else {
        UpdateUi();
    }
}

void CTomatoGGChatDlg::ResetPlayer(const UrlInfo& u, const std::wstring& key)
{
    CancelGeneration();
    m_data = PlayerData{};
    m_data.server = u.server;
    m_data.nick = u.nick;
    m_data.accountId = u.accountId;
    m_dataKey = key;
    m_history.clear();
    m_autoKey.clear();
    m_chat.Clear();
    AppendSystem(std::format(L"{} ({}) 페이지를 감지했어요.", u.nick, u.server));
}

// ═════════════════════════════════════════════════════════════
//  수집 상태 머신:  Overview → Battles → Done
//  (WNX/WN8 은 사이트에 표시된 모드를 그대로 따른다)
// ═════════════════════════════════════════════════════════════
void CTomatoGGChatDlg::StartScrape()
{
    KillTimer(kTimerPoll);
    KillTimer(kTimerModeWatch);
    ++m_scrapeGen;
    m_phase = Phase::Overview;
    m_pollCount = 0; m_stable = 0; m_lastRows = 0;
    m_pollBusy = false; m_watchBusy = false; m_nudged = false;
    m_pendingBattles.clear();
    m_data.overviewReady = m_data.battlesReady = m_data.scrapeComplete = false;
    m_data.battles.clear();
    SetTimer(kTimerPoll, 700, nullptr);
    UpdateUi();
}

void CTomatoGGChatDlg::StopScrape()
{
    KillTimer(kTimerPoll);
    KillTimer(kTimerModeWatch);
    ++m_scrapeGen;
    m_phase = Phase::Idle;
    m_pollBusy = m_watchBusy = false;
}

void CTomatoGGChatDlg::OnTimer(UINT_PTR id)
{
    if (id == kTimerPoll) {
        if (m_pollBusy || !m_onMain) return;
        ++m_pollCount;
        if (m_phase == Phase::Overview) PollOverview();
        else if (m_phase == Phase::Battles) PollBattles();
        return;
    }
    if (id == kTimerModeWatch) { CheckSiteMode(); return; }
    CDialogEx::OnTimer(id);
}

void CTomatoGGChatDlg::PollOverview()
{
    if (m_pollCount > 40) {                                    // ≈28초
        StopScrape();
        AppendSystem(L"전적 요약을 읽지 못했어요 (시간 초과). 페이지가 다 뜬 뒤 '새로고침'을 눌러 보세요.");
        UpdateUi();
        return;
    }
    m_pollBusy = true;
    const unsigned gen = m_scrapeGen;
    m_web.GetPageText([this, gen](std::wstring text) {
        m_pollBusy = false;
        if (gen != m_scrapeGen || m_phase != Phase::Overview) return;

        m_lastOverviewText = text;
        OverviewData ov;
        std::wstring err;
        if (!CWnxParser::ParseOverview(text, m_data.nick, ov, &err)) { m_lastParseErr = err; return; }   // 계속 폴링

        m_data.overview = ov;
        m_data.overviewReady = true;
        EnterBattlesPhase();
        });
}

void CTomatoGGChatDlg::EnterBattlesPhase()
{
    m_phase = Phase::Battles;
    m_pollCount = 0; m_stable = 0; m_lastRows = 0; m_nudged = false;
    AppendSystem(std::format(L"{} 요약 수집 완료. 최근 전투 목록을 읽는 중... (조금 걸릴 수 있어요)", RatingLabel(m_data.overview.mode)));
    UpdateUi();
}

void CTomatoGGChatDlg::PollBattles()
{
    if (m_pollCount > 70) {                                    // ≈49초
        FinishScrape(!m_pendingBattles.empty());
        return;
    }
    if (m_pollCount == 10 && !m_nudged) { m_nudged = true; m_web.NudgeBattlesIntoView(); }   // 지연 로딩 유도

    m_pollBusy = true;
    const unsigned gen = m_scrapeGen;
    m_web.ReadBattles([this, gen](std::string json) {
        m_pollBusy = false;
        if (gen != m_scrapeGen || m_phase != Phase::Battles) return;

        m_lastBattlesJson = json;
        std::vector<BattleRecord> rows;
        std::wstring err;
        if (!CWnxParser::ParseBattles(json, rows, &err)) { m_lastParseErr = err; m_stable = 0; return; }

        const size_t n = rows.size();
        if (n > 0 && n == m_lastRows) ++m_stable; else m_stable = 0;
        m_lastRows = n;
        if (n > 0) m_pendingBattles = std::move(rows);

        if (n > 0 && m_stable >= 2) FinishScrape(true);        // 행 수가 두 번 연속 같으면 로딩 완료로 본다
        else RefreshStatus();
        });
}

void CTomatoGGChatDlg::FinishScrape(bool battlesOk)
{
    KillTimer(kTimerPoll);
    m_pollBusy = false;
    m_phase = Phase::Done;
    if (battlesOk) {
        m_data.battles = m_pendingBattles;
        m_data.battlesReady = true;
        AppendSystem(std::format(L"최근 전투 {}판 수집 완료.", m_data.battles.size()));
    }
    else {
        AppendSystem(L"최근 전투 목록을 가져오지 못했어요. 요약 지표만으로 분석합니다.");
    }
    m_data.scrapeComplete = true;
    SetTimer(kTimerModeWatch, 1500, nullptr);                  // 사이트에서 WNX/WN8 탭을 직접 눌러도 따라가기
    TryAutoAnalyze();
    UpdateUi();
}

// 사용자가 웹뷰 안의 WNX|WN8 탭을 눌렀는지 감시 → 바뀌었으면 다시 읽고 재분석
void CTomatoGGChatDlg::CheckSiteMode()
{
    if (m_phase != Phase::Done || !m_onMain || m_watchBusy || !m_web.IsReady()) return;
    m_watchBusy = true;
    const unsigned gen = m_scrapeGen;
    m_web.ReadSiteMode([this, gen](std::wstring mode) {
        m_watchBusy = false;
        if (gen != m_scrapeGen || m_phase != Phase::Done || !m_onMain) return;
        RatingMode sm;
        if (EqualsNoCase(mode, L"WN8")) sm = RatingMode::Wn8;
        else if (EqualsNoCase(mode, L"WNX")) sm = RatingMode::Wnx;
        else return;
        if (sm == m_data.overview.mode) return;
        AppendSystem(std::format(L"사이트의 지표가 {} 로 바뀌었어요. 다시 읽는 중...", RatingLabel(sm)));
        StartScrape();
        });
}

// ═════════════════════════════════════════════════════════════
//  LLM 흐름
// ═════════════════════════════════════════════════════════════
void CTomatoGGChatDlg::TryAutoAnalyze()
{
    if (!m_onMain || !m_data.overviewReady) return;
    const std::wstring key = m_dataKey + L"|" + RatingLabel(m_data.overview.mode);
    if (key == m_autoKey) return;
    if (!m_llm.IsReady()) { m_autoPending = true; return; }
    m_autoPending = false;
    m_autoKey = key;
    StartAnalysis();
}

void CTomatoGGChatDlg::StartAnalysis()
{
    std::vector<ChatMsg> msgs;
    msgs.push_back({ "system", CPromptBuilder::BuildSystem(m_data, m_cfg) });
    msgs.push_back({ "user",   CPromptBuilder::BuildAnalysisRequest() });
    m_history = msgs;
    AppendSystem(std::format(L"{} 기준으로 분석 중...", RatingLabel(m_data.overview.mode)));
    RunLlm(std::move(msgs));
}

void CTomatoGGChatDlg::RunLlm(std::vector<ChatMsg> msgs)
{
    if (m_llm.IsBusy()) { m_queued = std::move(msgs); m_llm.Cancel(); return; }   // 끝나면 OnLlmDone 에서 이어서 실행
    m_assistantBuf.clear();
    if (m_llm.GenerateAsync(std::move(msgs), GetSafeHwnd(), m_cfg.maxNewTokens)) m_chat.BeginBot();
    else AppendSystem(L"모델이 아직 준비되지 않았어요.");
    UpdateUi();
}

void CTomatoGGChatDlg::CancelGeneration()
{
    m_queued.reset();
    if (m_llm.IsBusy()) m_llm.Cancel();
}

void CTomatoGGChatDlg::TrimHistory()
{
    auto total = [&] { size_t t = 0; for (auto& m : m_history) t += m.content.size(); return t; };
    while (m_history.size() > 2 && total() > (size_t)m_cfg.maxPromptBytes) {
        m_history.erase(m_history.begin() + 1);                                     // system 다음의 가장 오래된 메시지
        if (m_history.size() > 2 && m_history[1].role == "assistant") m_history.erase(m_history.begin() + 1);
    }
}

LRESULT CTomatoGGChatDlg::OnLlmLoaded(WPARAM ok, LPARAM lp)
{
    std::unique_ptr<std::wstring> msg((std::wstring*)lp);   // 성공: 장치 안내, 실패: 오류 문구
    if (ok) {
        AppendSystem(L"모델 로딩 완료.");
        if (msg && !msg->empty()) AppendSystem(*msg);        // 예) GPU 가속(Vulkan): Vulkan0 - NVIDIA ... / gpu_layers=99
        if (m_autoPending) TryAutoAnalyze();
    }
    else {
        AppendSystem(L"모델 로딩 실패: " + (msg ? *msg : std::wstring(L"알 수 없는 오류")));
        AppendSystem(L"TomatoGGChat.ini 의 model_dir / model_file 경로와 GGUF 파일을 확인하세요.");
    }
    UpdateUi();
    return 0;
}

LRESULT CTomatoGGChatDlg::OnLlmToken(WPARAM w, LPARAM)
{
    std::unique_ptr<std::wstring> p((std::wstring*)w);     // 반드시 해제
    m_chat.AppendToBot(*p);
    m_assistantBuf += *p;
    return 0;
}

LRESULT CTomatoGGChatDlg::OnLlmError(WPARAM w, LPARAM)
{
    std::unique_ptr<std::wstring> p((std::wstring*)w);
    AppendSystem(L"오류: " + *p);
    return 0;
}

LRESULT CTomatoGGChatDlg::OnLlmDone(WPARAM, LPARAM)
{
    if (m_llm.IsBusy()) return 0;                           // 이전 생성의 늦은 DONE (이미 새 생성이 시작됨)
    m_chat.EndBot();                                        // 토큰이 없었다면 빈 말풍선 제거
    if (m_queued) {                                         // 중단하고 새 요청으로 교체
        auto q = std::move(*m_queued);
        m_queued.reset();
        RunLlm(std::move(q));
        return 0;
    }
    if (!m_assistantBuf.empty()) m_history.push_back({ "assistant", ToUtf8(m_assistantBuf) });
    else if (!m_history.empty() && m_history.back().role == "user") m_history.pop_back();   // 답이 없으면 짝 맞추기
    m_assistantBuf.clear();
    UpdateUi();
    return 0;
}

// ═════════════════════════════════════════════════════════════
//  입력 / 버튼 / 명령어
// ═════════════════════════════════════════════════════════════
void CTomatoGGChatDlg::OnBnClickedSend()
{
    if (m_llm.IsBusy()) { CancelGeneration(); return; }     // 생성 중에는 '중지' 버튼
    SubmitInput();
}

void CTomatoGGChatDlg::SubmitInput()
{
    if (!InputEnabled()) return;
    CString raw;
    m_edit.GetWindowText(raw);
    raw.Trim();
    if (raw.IsEmpty()) return;
    const std::wstring text = (LPCWSTR)raw;

    if (text[0] == L'/') { m_edit.SetWindowText(L""); HandleCommand(text); return; }

    if (m_llm.IsBusy())   { AppendSystem(L"답변을 만드는 중이에요. 끝난 뒤에 보내 주세요. (중지 버튼으로 멈출 수 있어요)"); return; }
    if (!m_llm.IsReady()) { AppendSystem(L"모델을 아직 불러오는 중이에요."); return; }
    if (!CanAsk())        { AppendSystem(L"전적 수집이 끝나지 않았어요. 잠시만 기다려 주세요."); return; }

    m_edit.SetWindowText(L"");
    m_chat.AddUser(text);

    const std::string sys = CPromptBuilder::BuildSystem(m_data, m_cfg);
    if (m_history.empty() || m_history[0].role != "system") m_history.insert(m_history.begin(), ChatMsg{ "system", sys });
    else m_history[0].content = sys;

    m_history.push_back({ "user", ToUtf8(text) });
    TrimHistory();
    RunLlm(m_history);
}

void CTomatoGGChatDlg::OnBnClickedRefresh()
{
    if (!m_onMain || m_data.nick.empty()) return;
    m_autoKey.clear();                     // 다시 읽은 데이터로 재분석
    AppendSystem(L"페이지를 다시 읽는 중...");
    StartScrape();
}

void CTomatoGGChatDlg::HandleCommand(const std::wstring& cmdRaw)
{
    const std::wstring cmd = ToLower(Trim(cmdRaw));
    if (cmd == L"/help") {
        AppendSystem(L"/refresh  페이지를 다시 읽고 재분석");
        AppendSystem(L"/clear  대화 기록 비우기");
        AppendSystem(L"/devtools  웹뷰 개발자 도구 열기");
        AppendSystem(L"WNX/WN8 전환은 웹뷰 안의 탭을 직접 누르세요. 앱이 자동으로 따라갑니다.");
    }
    else if (cmd == L"/refresh") {
        OnBnClickedRefresh();
    }
    else if (cmd == L"/clear") {
        CancelGeneration();
        m_history.clear();
        m_chat.Clear();
        AppendSystem(L"대화 기록을 비웠어요.");
    }
    else if (cmd == L"/devtools") {
        m_web.OpenDevTools();
    }
    else {
        AppendSystem(L"알 수 없는 명령이에요. /help 를 입력해 보세요.");
    }
}

// ═════════════════════════════════════════════════════════════
//  UI 상태
// ═════════════════════════════════════════════════════════════
void CTomatoGGChatDlg::UpdateUi()
{
    if (!m_layoutReady || !m_edit.m_hWnd) return;

    const bool input = InputEnabled();
    const bool busy = m_llm.IsBusy();

    m_edit.EnableWindow(input);                          // Main 탭이 아니면 흐려지며 클릭 불가
    m_btnSend.EnableWindow(busy || input);
    m_btnSend.SetWindowText(busy ? L"중지" : L"전송");
    m_btnRefresh.EnableWindow(m_onMain && m_webReady);

    // 멀티라인 Edit 는 큐배너(회색 안내문)를 지원하지 않으므로, 비활성일 때 안내 문구를 본문에 흐리게 보여준다.
    if (!input) {
        CString cur;
        m_edit.GetWindowText(cur);
        if (cur.IsEmpty() || m_hintShown) {
            const wchar_t* hint = !m_webReady ? L"웹뷰 초기화 중..." : L"플레이어의 Main 탭에서만 대화할 수 있어요";
            if (cur != hint) m_edit.SetWindowText(hint);
            m_hintShown = true;
        }
    }
    else if (m_hintShown) {
        m_edit.SetWindowText(L"");
        m_hintShown = false;
    }

    RefreshStatus();
}

void CTomatoGGChatDlg::RefreshStatus()
{
    std::wstring s;
    if (!m_webReady)                 s = L"웹뷰 초기화 중...";
    else if (!m_url.IsPlayer())      s = L"Tomato.gg 에서 플레이어를 검색하세요.";
    else if (!m_onMain)              s = std::format(L"{} · Main 탭에서만 대화할 수 있어요.", m_url.nick);
    else if (m_phase == Phase::Overview) s = std::format(L"{} · 전적 요약 수집 중...", m_url.nick);
    else if (m_phase == Phase::Battles)  s = std::format(L"{} · 최근 전투 수집 중... ({}판 감지)", m_url.nick, m_lastRows);
    else if (m_phase == Phase::Done)
        s = std::format(L"{} · {} · 전투 {}판 · {}", m_url.nick, RatingLabel(m_data.overview.mode),
            m_data.battles.size(), m_llm.IsBusy() ? L"답변 생성 중" : (m_llm.IsReady() ? L"대화 가능" : L"모델 로딩 중"));
    else                             s = L"수집 실패 - '새로고침'을 눌러 보세요.";
    m_status.SetWindowText(s.c_str());
}
