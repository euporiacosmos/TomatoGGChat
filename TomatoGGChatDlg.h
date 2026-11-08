#pragma once
#include <optional>
#include "Models.h"
#include "Config.h"
#include "WebViewHost.h"
#include "LlmEngine.h"
#include "ChatView.h"

class CTomatoGGChatDlg : public CDialogEx
{
public:
    CTomatoGGChatDlg(CWnd* pParent = nullptr);
    enum { IDD = IDD_TOMATOGGCHAT_DIALOG };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual BOOL PreTranslateMessage(MSG* pMsg);

    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnDestroy();
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnEnSetfocusEditInput();
    afx_msg void OnEnChangeEditInput();       // 줄 수가 바뀌면 입력창 높이를 다시 계산
    afx_msg void OnBnClickedSend();            // 평소엔 '전송', 답변 생성 중에는 '중지'
    afx_msg void OnBnClickedRefresh();
    afx_msg LRESULT OnLlmLoaded(WPARAM, LPARAM);
    afx_msg LRESULT OnLlmToken(WPARAM, LPARAM);
    afx_msg LRESULT OnLlmDone(WPARAM, LPARAM);
    afx_msg LRESULT OnLlmError(WPARAM, LPARAM);
    DECLARE_MESSAGE_MAP()

private:
    // ── 컨트롤 (DDX_Control 로 연결) ──────────────────────────
    CEdit     m_edit;            // IDC_EDIT_INPUT  (멀티라인)
    CButton   m_btnSend;         // IDC_BTN_SEND    (입력창 오른쪽 아래에 겹쳐 놓는다)
    CButton   m_btnRefresh;      // IDC_BTN_REFRESH
    CStatic   m_status;          // IDC_STATIC_STATUS

    // ── 코드로 만드는 컨트롤 ───────────────────────────────────
    CChatView m_chat;            // 말풍선(Static) + 오른쪽 스크롤바
    static constexpr UINT kChatViewId = 41001;

    // ── 핵심 객체 ──────────────────────────────────────────────
    CWebViewHost m_web;          // ★ WebView2 (이 변수가 m_web)
    CLlmEngine   m_llm;          // llama.cpp
    AppConfig    m_cfg;
    HICON        m_hIcon = nullptr;

    // ── 레이아웃 ───────────────────────────────────────────────
    static constexpr int kSendW = 72, kSendH = 26;
    // 입력창 높이 = (현재 줄 수 + 다음 줄을 위한 여유 1줄). 줄바꿈(Shift+Enter)마다 한 줄 높이만큼 커지고,
    // 그만큼 채팅창 세로 높이만 줄어든다. (웹뷰 높이는 그대로)
    static constexpr int kInputMinLines = 2;     // 글 1줄 + 여유 1줄
    static constexpr int kInputMaxLines = 8;     // 이 이상은 입력창 안에서 스크롤
    static constexpr int kInputPad = 26;         // 테두리/여백
    static constexpr int kChatMinH = 80;         // 채팅창 최소 높이
    int  m_inputH = 0;                 // 현재 입력창 높이(px)
    int  InputLineHeight();            // 입력창 글꼴의 한 줄 높이(px)
    int  InputHeightForLines(int lines);
    int  CalcInputHeight();
    bool  m_layoutReady = false;
    bool  m_webReady = false;
    bool  m_wideLayout = false;  // 가로:세로 ≥ 4:3 이면 true → 채팅이 오른쪽
    CRect m_rcWeb;
    void LayoutChildren();

    // ── 마우스 휠 대상: 마지막으로 클릭한 쪽 ─────────────────────
    enum class WheelTarget { Chat, Web };
    WheelTarget m_wheelTarget = WheelTarget::Chat;

    // ── 페이지/데이터 상태 ─────────────────────────────────────
    enum class Phase { Idle, Overview, Battles, Done };
    static constexpr UINT_PTR kTimerPoll = 1;
    static constexpr UINT_PTR kTimerModeWatch = 2;

    UrlInfo      m_url;
    bool         m_onMain = false;
    std::wstring m_dataKey;                    // "서버|계정ID"
    PlayerData   m_data;

    Phase        m_phase = Phase::Idle;
    unsigned     m_scrapeGen = 0;              // 오래된 비동기 콜백 무시용
    int          m_pollCount = 0, m_stable = 0;
    size_t       m_lastRows = 0;
    bool         m_pollBusy = false, m_watchBusy = false, m_nudged = false;
    std::vector<BattleRecord> m_pendingBattles;
    std::wstring m_lastOverviewText, m_lastParseErr;
    std::string  m_lastBattlesJson;

    void OnTomatoUrl(const std::wstring& url);
    void ResetPlayer(const UrlInfo& u, const std::wstring& key);
    void StartScrape();
    void StopScrape();
    void PollOverview();
    void EnterBattlesPhase();
    void PollBattles();
    void FinishScrape(bool battlesOk);
    void CheckSiteMode();                      // 사이트에서 WNX|WN8 탭을 직접 눌렀는지 감시

    // ── LLM / 대화 ─────────────────────────────────────────────
    std::vector<ChatMsg> m_history;            // [0]=system, 이후 user/assistant 교대
    std::wstring         m_assistantBuf;
    std::optional<std::vector<ChatMsg>> m_queued;
    std::wstring         m_autoKey;            // 같은 (플레이어, 지표)는 자동 분석 1회만
    bool                 m_autoPending = false;
    bool                 m_hintShown = false;  // 비활성 상태의 입력창에 안내 문구를 표시 중인가

    bool InputEnabled() const { return m_webReady && m_onMain; }                        // 입력창 활성 조건: Main 탭
    bool CanAsk() const { return m_onMain && m_data.overviewReady && m_llm.IsReady(); } // 모델 호출 가능 조건
    void TryAutoAnalyze();
    void StartAnalysis();
    void RunLlm(std::vector<ChatMsg> msgs);
    void CancelGeneration();
    void TrimHistory();
    void SubmitInput();
    void HandleCommand(const std::wstring& cmd);

    // ── UI 보조 ────────────────────────────────────────────────
    void UpdateUi();
    void RefreshStatus();
    void AppendSystem(const std::wstring& t) { m_chat.AddSystem(t); }
    bool IsWebViewWindow(HWND h) const;
};