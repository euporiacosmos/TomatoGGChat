#pragma once
// ─────────────────────────────────────────────────────────────
// 채팅 출력창 (RichEdit 미사용)
//  · 자체 자식 윈도우(WS_VSCROLL = 오른쪽 스크롤바) 안에
//    말풍선용 Static 컨트롤(SS_OWNERDRAW)을 동적으로 추가해서 쌓는다.
//  · 스크롤바/마우스 휠/키보드로 말풍선 영역을 스크롤한다.
// ─────────────────────────────────────────────────────────────
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ChatColors {
    constexpr COLORREF Bg       = RGB(0, 0, 0);
    constexpr COLORREF Text     = RGB(0, 0, 0);
    constexpr COLORREF Sys      = RGB(141, 141, 141);
    constexpr COLORREF UserFill = RGB(131, 131, 131);
    constexpr COLORREF BotFill  = RGB(131, 131, 131);
}

enum class ChatRole { System, User, Bot };

// 말풍선 하나 = Static 컨트롤 하나 (그리기는 CChatView::OnDrawItem 에서 위임)
class CChatBubble : public CStatic
{
public:
    void Setup(ChatRole role, CFont* font, CFont* boldFont) { m_role = role; m_font = font; m_bold = boldFont; }
    void SetBubbleText(const std::wstring& t) { m_text = t; if (m_hWnd) Invalidate(FALSE); }
    void DrawBubble(HDC hdc, const CRect& rcItem);

    // maxW 안에 들어가는 말풍선 크기 계산 (짧은 글은 좁게, 긴 글은 maxW 에서 줄바꿈)
    static CSize Measure(CDC& dc, CFont* font, CFont* boldFont, const std::wstring& text, ChatRole role, int maxW);

private:
    static int PadX(ChatRole r);
    static int PadY(ChatRole r);
    // 말풍선 맨 앞에 붙는 볼드 접두어 ("AI > " / "사용자 > ", 시스템 메시지는 없음)
    static const wchar_t* PrefixOf(ChatRole r);

    ChatRole     m_role = ChatRole::System;
    CFont*       m_font = nullptr;
    CFont*       m_bold = nullptr;
    std::wstring m_text;
};

class CChatView : public CWnd
{
public:
    CChatView() = default;

    BOOL CreateView(CWnd* parent, UINT id, const CRect& rc);

    // 사용자가 채팅창을 클릭/포커스하면 호출 (부모가 '휠 대상'을 채팅으로 바꾸는 데 사용)
    std::function<void()> OnClicked;

    void Clear();
    void AddSystem(const std::wstring& t) { AddItem(ChatRole::System, t); }
    void AddUser(const std::wstring& t)   { AddItem(ChatRole::User, t); }

    // 스트리밍 답변: BeginBot → AppendToBot(여러 번) → EndBot
    void BeginBot();
    void AppendToBot(const std::wstring& chunk);
    void EndBot();

    void ScrollByWheel(short wheelDelta);   // WM_MOUSEWHEEL 의 delta

protected:
    DECLARE_MESSAGE_MAP()
    afx_msg int  OnCreate(LPCREATESTRUCT cs);
    afx_msg void OnDestroy();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnSetFocus(CWnd* pOldWnd);
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDIS);

private:
    struct Item {
        std::unique_ptr<CChatBubble> bubble;
        ChatRole     role = ChatRole::System;
        std::wstring text;
        bool dirty = true;        // 텍스트가 바뀌어 재측정 필요
        int  cachedW = -1;        // 마지막 측정에 사용한 컨테이너 폭
        int  x = 0, y = 0, w = 0, h = 0;   // y 는 스크롤 전 절대 좌표
    };

    void AddItem(ChatRole role, const std::wstring& text);
    void RemoveItem(size_t index);
    void Relayout();
    void PositionItems(const CRect& rcClient);
    void SetScrollPosition(int pos);
    int  MaxPos() const;

    CFont m_font;
    CFont m_fontBold;             // 접두어(AI > / 사용자 > )용
    std::vector<Item> m_items;
    int   m_total = 0;            // 전체 콘텐츠 높이
    int   m_pos = 0;              // 현재 스크롤 위치(px)
    bool  m_stick = true;         // 맨 아래를 따라가는 중인가
    bool  m_inLayout = false;
    int   m_curBot = -1;          // 스트리밍 중인 말풍선 인덱스
    bool  m_botPlaceholder = false;
};
