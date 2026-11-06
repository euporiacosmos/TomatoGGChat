#include "pch.h"
#include "ChatView.h"

namespace {
constexpr int    kMarginX = 8;
constexpr int    kMarginY = 8;
constexpr int    kGap = 8;
constexpr int    kWheelStep = 60;      // 휠 한 칸(120)당 스크롤 px
constexpr size_t kMaxItems = 300;      // 오래된 말풍선부터 제거
}

// ═════════════════════════════════════════════════════════════
//  CChatBubble
// ═════════════════════════════════════════════════════════════
int CChatBubble::PadX(ChatRole r) { return r == ChatRole::System ? 2 : 12; }
int CChatBubble::PadY(ChatRole r) { return r == ChatRole::System ? 2 : 8; }

const wchar_t* CChatBubble::PrefixOf(ChatRole r)
{
    switch (r) {
    case ChatRole::Bot:  return L"AI > ";
    case ChatRole::User: return L"사용자 > ";
    default:             return L"";
    }
}

// ── 가벼운 마크다운 + 줄바꿈 ────────────────────────────────────
// 모델이 자주 쓰는 서식만 보여 준다:  **굵게**,  ## 제목(굵게),  * 항목(•)
// Static 컨트롤/DrawText 는 한 번에 폰트 하나만 쓰므로, 굵은 글자가 섞인 문단은 직접 줄바꿈하며 그린다.
namespace {

struct Run         { std::wstring text; bool bold = false; };
struct LogicalLine { std::vector<Run> runs; bool bullet = false; };      // 원문의 한 줄
struct Piece       { std::wstring text; bool bold = false; int x = 0; };  // 같은 굵기로 이어서 그릴 조각
struct VisLine     { std::vector<Piece> pieces; int width = 0; };         // 화면에 보이는 한 줄

// \r 제거, 탭은 공백으로
std::wstring Normalize(const std::wstring& s)
{
    std::wstring r;
    r.reserve(s.size());
    for (wchar_t c : s) {
        if (c == L'\r') continue;
        r.push_back(c == L'\t' ? L' ' : c);
    }
    return r;
}

// "**" 를 만날 때마다 굵게를 켜고 끈다. (스트리밍 중 짝이 안 맞으면 줄 끝까지 굵게 유지)
void AppendInline(std::vector<Run>& runs, const std::wstring& s, bool forceBold)
{
    bool bold = false;
    std::wstring cur;
    auto flush = [&] {
        if (!cur.empty()) { runs.push_back({ cur, bold || forceBold }); cur.clear(); }
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'*' && i + 1 < s.size() && s[i + 1] == L'*') { flush(); bold = !bold; ++i; continue; }
        cur.push_back(s[i]);
    }
    flush();
}

// 모델이 자주 쓰는 마크다운만 해석한다:  "## 제목" → 굵게,  "**굵게**",  "* 항목" / "- 항목" → "• 항목"
// markdown=false 이면(사용자/시스템 메시지) 글자를 그대로 둔다.
std::vector<LogicalLine> ParseText(const std::wstring& text, bool markdown)
{
    std::vector<LogicalLine> out;
    size_t pos = 0;
    while (true) {
        const size_t nl = text.find(L'\n', pos);
        const std::wstring line = text.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        LogicalLine ll;

        if (!markdown) {
            if (!line.empty()) ll.runs.push_back({ line, false });
        } else {
            size_t b = 0;
            while (b < line.size() && line[b] == L' ') ++b;                 // 들여쓰기는 무시
            std::wstring body = line.substr(b);
            bool heading = false;

            size_t h = 0;
            while (h < body.size() && body[h] == L'#') ++h;
            if (h >= 1 && h <= 6 && h < body.size() && body[h] == L' ') {   // "## 제목"
                heading = true;
                body = body.substr(h + 1);
            } else if (body.size() >= 2 && (body[0] == L'*' || body[0] == L'-' || body[0] == L'+') && body[1] == L' ') {
                ll.bullet = true;                                           // "* 항목" ("**굵게**" 는 두 번째 글자가 '*' 라서 해당 안 됨)
                body = body.substr(2);
                ll.runs.push_back({ L"• ", false });
            }
            while (!body.empty() && body[0] == L' ') body.erase(0, 1);
            AppendInline(ll.runs, body, heading);
        }
        out.push_back(std::move(ll));
        if (nl == std::wstring::npos) break;
        pos = nl + 1;
    }
    return out;
}

int TextWidth(CDC& dc, CFont* f, const wchar_t* p, size_t n)
{
    if (n == 0) return 0;
    CFont* old = f ? dc.SelectObject(f) : nullptr;
    SIZE sz = {};
    ::GetTextExtentPoint32W(dc.GetSafeHdc(), p, (int)n, &sz);
    if (old) dc.SelectObject(old);
    return sz.cx;
}

// avail 폭 안에 들어가는 글자 수
int FitChars(CDC& dc, CFont* f, const std::wstring& s, int avail)
{
    CFont* old = f ? dc.SelectObject(f) : nullptr;
    int fit = 0;
    SIZE sz = {};
    ::GetTextExtentExPointW(dc.GetSafeHdc(), s.c_str(), (int)s.size(), (std::max)(avail, 1), &fit, nullptr, &sz);
    if (old) dc.SelectObject(old);
    return fit;
}

int LineHeight(CDC& dc, CFont* reg, CFont* bold)
{
    int h = 0;
    for (CFont* f : { reg, bold }) {
        if (!f) continue;
        CFont* old = dc.SelectObject(f);
        TEXTMETRIC tm = {};
        dc.GetTextMetrics(&tm);
        dc.SelectObject(old);
        h = (std::max)(h, (int)tm.tmHeight);
    }
    return (std::max)(h, 1);
}

// 굵은 글자가 섞여도 한 문단처럼 보이도록 직접 줄바꿈한다.
//  · 공백에서 끊고, 낱말 하나가 한 줄보다 길면 글자 단위로 자른다.
//  · firstIndent: 첫 줄 앞에 붙는 볼드 접두어의 폭.  불릿 항목은 둘째 줄부터 "• " 폭만큼 들여 쓴다.
std::vector<VisLine> Layout(CDC& dc, CFont* reg, CFont* bold, const std::vector<LogicalLine>& lines,
                            int firstIndent, int maxW, bool* wrapped)
{
    std::vector<VisLine> out;
    if (wrapped) *wrapped = false;
    bool firstOfAll = true;
    auto fontOf = [&](bool b) { return b ? bold : reg; };
    auto widthOf = [&](const std::wstring& s, bool b) { return TextWidth(dc, fontOf(b), s.c_str(), s.size()); };

    for (const LogicalLine& L : lines) {
        const int hang = (L.bullet && !L.runs.empty()) ? widthOf(L.runs[0].text, L.runs[0].bold) : 0;
        VisLine cur;
        int x = firstOfAll ? firstIndent : 0;
        int endX = x;                // 마지막 '글자' 끝 (뒤쪽 공백 제외)
        bool ink = false;            // 이 화면 줄에 글자가 하나라도 놓였는가
        firstOfAll = false;

        auto flushLine = [&](bool soft) {
            cur.width = endX;
            out.push_back(std::move(cur));
            cur = VisLine();
            x = endX = hang;
            ink = false;
            if (soft && wrapped) *wrapped = true;
        };
        auto addPiece = [&](const std::wstring& s, bool b, int w, bool isSpace) {
            if (!cur.pieces.empty() && cur.pieces.back().bold == b) cur.pieces.back().text += s;
            else cur.pieces.push_back({ s, b, x });
            x += w;
            if (!isSpace) { endX = x; ink = true; }
        };

        for (const Run& r : L.runs) {
            size_t i = 0;
            while (i < r.text.size()) {
                const bool sp = (r.text[i] == L' ');
                size_t j = i;
                while (j < r.text.size() && (r.text[j] == L' ') == sp) ++j;
                std::wstring atom = r.text.substr(i, j - i);
                i = j;
                int aw = widthOf(atom, r.bold);

                if (sp) {
                    if (!ink) continue;                 // 줄 맨 앞 공백은 버린다
                    if (x + aw > maxW) continue;        // 줄 끝에서 넘치는 공백도 버린다
                    addPiece(atom, r.bold, aw, true);
                    continue;
                }

                if (x + aw > maxW && ink) flushLine(true);          // 낱말이 안 들어가면 다음 줄로
                while (!atom.empty() && x + aw > maxW) {            // 그래도 한 줄보다 길면 글자 단위로 자른다
                    int fit = FitChars(dc, fontOf(r.bold), atom, maxW - x);
                    if (fit <= 0) fit = 1;                          // 최소 한 글자는 놓는다 (무한 루프 방지)
                    if (fit >= (int)atom.size()) break;
                    const std::wstring head = atom.substr(0, fit);
                    addPiece(head, r.bold, widthOf(head, r.bold), false);
                    atom.erase(0, fit);
                    flushLine(true);
                    aw = widthOf(atom, r.bold);
                }
                if (!atom.empty()) addPiece(atom, r.bold, aw, false);
            }
        }
        cur.width = endX;
        out.push_back(std::move(cur));
    }
    if (out.empty()) out.push_back(VisLine());
    return out;
}

} // namespace

CSize CChatBubble::Measure(CDC& dc, CFont* font, CFont* boldFont, const std::wstring& rawText, ChatRole role, int maxW)
{
    const int px = PadX(role), py = PadY(role);
    CFont* bold = boldFont ? boldFont : font;
    const std::wstring text = Normalize(rawText);

    // 볼드 접두어의 폭
    const wchar_t* prefix = PrefixOf(role);
    const int prefixW = prefix[0] ? TextWidth(dc, bold, prefix, wcslen(prefix)) : 0;

    const int innerMax = (std::max)(40, maxW - 2 * px);
    bool wrapped = false;
    const auto lines = Layout(dc, font, bold, ParseText(text, role == ChatRole::Bot), prefixW, innerMax, &wrapped);

    int textW = innerMax;
    if (!wrapped) {                                  // 줄바꿈이 없었다면 가장 긴 줄에 맞춰 폭을 줄인다
        int mx = prefixW;
        for (const auto& l : lines) mx = (std::max)(mx, l.width);
        textW = (std::min)(mx + 2, innerMax);
    }
    const int textH = (int)lines.size() * LineHeight(dc, font, bold);
    return CSize(textW + 2 * px, textH + 2 * py);
}

void CChatBubble::DrawBubble(HDC hdc, const CRect& rcItem)
{
    const int w = rcItem.Width(), h = rcItem.Height();
    if (w <= 0 || h <= 0) return;

    CDC* pdc = CDC::FromHandle(hdc);
    CDC mem;                                   // 더블 버퍼링 (스트리밍 중 깜빡임 방지)
    mem.CreateCompatibleDC(pdc);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(pdc, w, h);
    CBitmap* oldBmp = mem.SelectObject(&bmp);

    const CRect box(0, 0, w, h);
    mem.FillSolidRect(box, ChatColors::Bg);

    COLORREF textColor = ChatColors::Text;
    if (m_role == ChatRole::System) {
        textColor = ChatColors::Sys;
    } else {
        CBrush br(m_role == ChatRole::User ? ChatColors::UserFill : ChatColors::BotFill);
        CPen pen(PS_NULL, 0, RGB(0, 0, 0));
        CBrush* ob = mem.SelectObject(&br);
        CPen*   op = mem.SelectObject(&pen);
        mem.RoundRect(box, CPoint(16, 16));
        mem.SelectObject(ob);
        mem.SelectObject(op);
    }

    CFont* bold = m_bold ? m_bold : m_font;
    CFont* of = m_font ? mem.SelectObject(m_font) : nullptr;
    mem.SetBkMode(TRANSPARENT);
    mem.SetTextColor(textColor);

    CRect tr = box;
    tr.DeflateRect(PadX(m_role), PadY(m_role));

    // 1) 볼드 접두어 ("AI > " / "사용자 > ")
    int prefixW = 0;
    const wchar_t* prefix = PrefixOf(m_role);
    if (prefix[0]) {
        prefixW = TextWidth(mem, bold, prefix, wcslen(prefix));
        if (bold) mem.SelectObject(bold);
        mem.TextOut(tr.left, tr.top, prefix, (int)wcslen(prefix));
        if (m_font) mem.SelectObject(m_font);
    }

    // 2) 본문: 첫 줄은 접두어 뒤에서(조각의 x 에 이미 반영됨), 나머지 줄은 왼쪽 끝에서 시작
    const std::wstring text = Normalize(m_text);
    const auto lines = Layout(mem, m_font, bold, ParseText(text, m_role == ChatRole::Bot), prefixW, tr.Width(), nullptr);
    const int lineH = LineHeight(mem, m_font, bold);
    int y = tr.top;
    for (const auto& line : lines) {
        for (const auto& p : line.pieces) {
            if (p.text.empty()) continue;
            mem.SelectObject(p.bold ? bold : m_font);
            mem.TextOut(tr.left + p.x, y, p.text.c_str(), (int)p.text.size());
        }
        y += lineH;
    }

    if (of) mem.SelectObject(of);
    pdc->BitBlt(rcItem.left, rcItem.top, w, h, &mem, 0, 0, SRCCOPY);
    mem.SelectObject(oldBmp);
}

// ═════════════════════════════════════════════════════════════
//  CChatView
// ═════════════════════════════════════════════════════════════
BEGIN_MESSAGE_MAP(CChatView, CWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_SIZE()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_LBUTTONDOWN()
    ON_WM_SETFOCUS()
    ON_WM_KEYDOWN()
    ON_WM_ERASEBKGND()
    ON_WM_DRAWITEM()
END_MESSAGE_MAP()

BOOL CChatView::CreateView(CWnd* parent, UINT id, const CRect& rc)
{
    static const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    return CWnd::Create(cls, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                        rc, parent, id);
}

int CChatView::OnCreate(LPCREATESTRUCT cs)
{
    if (CWnd::OnCreate(cs) == -1) return -1;
    m_font.CreatePointFont(105, L"Malgun Gothic");     // 10.5pt
    LOGFONT lf = {};
    m_font.GetLogFont(&lf);
    lf.lfWeight = FW_BOLD;                             // 접두어용 볼드
    m_fontBold.CreateFontIndirect(&lf);
    return 0;
}

void CChatView::OnDestroy()
{
    for (auto& it : m_items) if (it.bubble && it.bubble->m_hWnd) it.bubble->DestroyWindow();
    m_items.clear();
    CWnd::OnDestroy();
}

BOOL CChatView::OnEraseBkgnd(CDC* pDC)
{
    CRect rc;
    GetClientRect(&rc);
    pDC->FillSolidRect(rc, ChatColors::Bg);
    return TRUE;
}

void CChatView::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    Relayout();
}

// owner-draw Static 의 WM_DRAWITEM 은 부모(이 뷰)로 온다 → 해당 말풍선에게 그리기를 위임
void CChatView::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDIS)
{
    if (lpDIS && lpDIS->CtlType == ODT_STATIC) {
        auto* b = reinterpret_cast<CChatBubble*>(::GetWindowLongPtr(lpDIS->hwndItem, GWLP_USERDATA));
        if (b) { b->DrawBubble(lpDIS->hDC, CRect(lpDIS->rcItem)); return; }
    }
    CWnd::OnDrawItem(nIDCtl, lpDIS);
}

// ── 항목 추가/삭제 ────────────────────────────────────────────
void CChatView::AddItem(ChatRole role, const std::wstring& text)
{
    if (!m_hWnd) return;

    Item it;
    it.role = role;
    it.text = text;
    it.bubble = std::make_unique<CChatBubble>();
    it.bubble->Setup(role, &m_font, &m_fontBold);
    it.bubble->SetBubbleText(text);
    if (!it.bubble->Create(L"", WS_CHILD | SS_OWNERDRAW, CRect(0, 0, 10, 10), this)) return;
    ::SetWindowLongPtr(it.bubble->m_hWnd, GWLP_USERDATA, (LONG_PTR)it.bubble.get());
    m_items.push_back(std::move(it));

    while (m_items.size() > kMaxItems) RemoveItem(0);
    Relayout();
}

void CChatView::RemoveItem(size_t index)
{
    if (index >= m_items.size()) return;
    if (m_items[index].bubble && m_items[index].bubble->m_hWnd) m_items[index].bubble->DestroyWindow();
    m_items.erase(m_items.begin() + index);
    if (m_curBot == (int)index) { m_curBot = -1; m_botPlaceholder = false; }
    else if (m_curBot > (int)index) --m_curBot;
}

void CChatView::Clear()
{
    for (auto& it : m_items) if (it.bubble && it.bubble->m_hWnd) it.bubble->DestroyWindow();
    m_items.clear();
    m_curBot = -1;
    m_botPlaceholder = false;
    m_pos = 0;
    m_stick = true;
    Relayout();
}

void CChatView::BeginBot()
{
    EndBot();                                   // 혹시 남은 말풍선 정리
    AddItem(ChatRole::Bot, L"…");
    m_curBot = (int)m_items.size() - 1;
    m_botPlaceholder = true;
}

void CChatView::AppendToBot(const std::wstring& chunk)
{
    if (chunk.empty()) return;
    if (m_curBot < 0 || m_curBot >= (int)m_items.size()) BeginBot();
    Item& it = m_items[m_curBot];
    if (m_botPlaceholder) { it.text.clear(); m_botPlaceholder = false; }
    it.text += chunk;
    it.dirty = true;
    it.bubble->SetBubbleText(it.text);
    Relayout();
}

void CChatView::EndBot()
{
    if (m_curBot >= 0 && m_curBot < (int)m_items.size() && m_botPlaceholder) RemoveItem(m_curBot);   // 토큰이 하나도 없었다면 제거
    m_curBot = -1;
    m_botPlaceholder = false;
    Relayout();
}

// ── 레이아웃 / 스크롤 ─────────────────────────────────────────
int CChatView::MaxPos() const
{
    if (!m_hWnd) return 0;
    CRect rc;
    GetClientRect(&rc);
    return (std::max)(0, m_total - (int)rc.Height());
}

void CChatView::Relayout()
{
    if (!m_hWnd || !m_font.m_hObject || !m_fontBold.m_hObject || m_inLayout) return;
    m_inLayout = true;

    CRect rc;
    GetClientRect(&rc);
    if (rc.Width() > 40 && rc.Height() > 10) {
        CClientDC dc(this);
        // 스크롤바가 생기거나 사라지면 클라이언트 폭이 바뀌므로 최대 3번 반복
        for (int pass = 0; pass < 3; ++pass) {
            const int W = rc.Width();
            int y = kMarginY;
            for (auto& it : m_items) {
                if (it.dirty || it.cachedW != W) {
                    const int maxW = (it.role == ChatRole::System) ? (W - 2 * kMarginX)
                                                                   : (int)((W - 2 * kMarginX) * 0.92);
                    const CSize sz = CChatBubble::Measure(dc, &m_font, &m_fontBold, it.text, it.role, maxW);
                    it.w = sz.cx;
                    it.h = sz.cy;
                    it.cachedW = W;
                    it.dirty = false;
                }
                it.y = y;
                y += it.h + kGap;
            }
            m_total = m_items.empty() ? 0 : (y - kGap + kMarginY);

            const int page = rc.Height();
            const int maxPos = (std::max)(0, m_total - page);
            m_pos = m_stick ? maxPos : (std::min)((std::max)(m_pos, 0), maxPos);

            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            si.nMin = 0;
            si.nMax = (std::max)(0, m_total - 1);
            si.nPage = (UINT)page;
            si.nPos = m_pos;
            SetScrollInfo(SB_VERT, &si, TRUE);

            CRect rc2;
            GetClientRect(&rc2);
            const bool same = (rc2.Width() == rc.Width());
            rc = rc2;
            if (same) break;
            for (auto& it : m_items) it.cachedW = -1;
        }
        PositionItems(rc);
    }
    m_inLayout = false;
}

void CChatView::PositionItems(const CRect& rc)
{
    const int W = rc.Width();
    for (auto& it : m_items) {
        it.x = (it.role == ChatRole::User) ? (std::max)(kMarginX, W - kMarginX - it.w) : kMarginX;
        const int top = it.y - m_pos;
        const bool visible = (top + it.h > 0) && (top < rc.Height());
        it.bubble->SetWindowPos(nullptr, it.x, top, it.w, it.h,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void CChatView::SetScrollPosition(int pos)
{
    const int maxPos = MaxPos();
    pos = (std::min)((std::max)(pos, 0), maxPos);
    m_stick = (pos >= maxPos);
    if (pos == m_pos) return;
    m_pos = pos;

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    si.nPos = m_pos;
    SetScrollInfo(SB_VERT, &si, TRUE);

    CRect rc;
    GetClientRect(&rc);
    PositionItems(rc);
}

void CChatView::ScrollByWheel(short wheelDelta)
{
    SetScrollPosition(m_pos - wheelDelta * kWheelStep / WHEEL_DELTA);
}

void CChatView::OnVScroll(UINT nSBCode, UINT /*nPos*/, CScrollBar* /*pScrollBar*/)
{
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(SB_VERT, &si);

    int pos = si.nPos;
    switch (nSBCode) {
    case SB_TOP:       pos = 0; break;
    case SB_BOTTOM:    pos = MaxPos(); break;
    case SB_LINEUP:    pos -= 30; break;
    case SB_LINEDOWN:  pos += 30; break;
    case SB_PAGEUP:    pos -= (int)(si.nPage * 0.9); break;
    case SB_PAGEDOWN:  pos += (int)(si.nPage * 0.9); break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    default: return;
    }
    SetScrollPosition(pos);
}

BOOL CChatView::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint /*pt*/)
{
    ScrollByWheel(zDelta);
    return TRUE;
}

void CChatView::OnLButtonDown(UINT nFlags, CPoint point)
{
    SetFocus();                                  // 웹뷰에서 포커스를 가져온다
    if (OnClicked) OnClicked();
    CWnd::OnLButtonDown(nFlags, point);
}

void CChatView::OnSetFocus(CWnd* pOldWnd)
{
    CWnd::OnSetFocus(pOldWnd);
    if (OnClicked) OnClicked();
}

void CChatView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar) {
    case VK_UP:    SetScrollPosition(m_pos - 30); return;
    case VK_DOWN:  SetScrollPosition(m_pos + 30); return;
    case VK_PRIOR: OnVScroll(SB_PAGEUP, 0, nullptr); return;
    case VK_NEXT:  OnVScroll(SB_PAGEDOWN, 0, nullptr); return;
    case VK_HOME:  SetScrollPosition(0); return;
    case VK_END:   SetScrollPosition(MaxPos()); return;
    }
    CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}
