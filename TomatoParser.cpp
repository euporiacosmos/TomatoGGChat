#include "pch.h"
#include "TomatoParser.h"
#include "Util.h"
#include "json.hpp"
#include <regex>
#include <filesystem>
#include <map>

namespace {

using Columns = std::vector<std::vector<std::wstring>>;
enum class MergeRule { None, Percent, Pair };

// ── 토큰화: 줄바꿈/탭 단위로 쪼개 공백 제거 ───────────────────
std::vector<std::wstring> Tokenize(const std::wstring& text)
{
    std::vector<std::wstring> out;
    std::wstring cur;
    auto flush = [&] {
        std::wstring t = Trim(cur);
        if (!t.empty()) out.push_back(std::move(t));
        cur.clear();
    };
    for (wchar_t c : text) {
        if (c == L'\n' || c == L'\r' || c == L'\t') flush();
        else cur.push_back(c);
    }
    flush();
    return out;
}

// "1,909" "2.1K" "3.9M" "-12" → double
bool ParseNumber(const std::wstring& raw, double& v)
{
    std::wstring s = Trim(raw);
    if (s.empty()) return false;
    double mul = 1;
    wchar_t last = s.back();
    if (last == L'K' || last == L'k')      { mul = 1e3; s.pop_back(); }
    else if (last == L'M' || last == L'm') { mul = 1e6; s.pop_back(); }
    else if (last == L'B' || last == L'b') { mul = 1e9; s.pop_back(); }
    s.erase(std::remove(s.begin(), s.end(), L','), s.end());
    if (s.empty()) return false;
    wchar_t* end = nullptr;
    double d = std::wcstod(s.c_str(), &end);
    if (end == s.c_str() || *end != L'\0') return false;
    v = d * mul;
    return true;
}
bool IsPercent(const std::wstring& tok, double& v)
{
    if (tok.empty() || tok.back() != L'%') return false;
    return ParseNumber(tok.substr(0, tok.size() - 1), v);
}
bool IsPlainNumber(const std::wstring& tok, double& v)
{
    if (tok.find(L'%') != std::wstring::npos) return false;
    return ParseNumber(tok, v);
}

bool IsPeriodLabel(const std::wstring& s)
{
    static const std::wregex re(L"^(Overall|\\d+\\s*Days?|\\d+\\s*Battles?)$", std::regex::icase);
    return std::regex_match(s, re);
}

bool IsRowLabel(const std::wstring& s)
{
    static const wchar_t* kLabels[] = { L"Battles", L"Avg Tier", L"WNX", L"WN8", L"Wins", L"Survived",
                                        L"Damage", L"Assist", L"Damage Ratio", L"Frags", L"K/D Ratio", L"Tanks" };
    for (auto* l : kLabels) if (EqualsNoCase(s, l)) return true;
    return false;
}

// 한 행의 토큰들을 "열" 단위로 묶는다.
//  Percent: "2.1K","48.9%" 처럼 % 토큰은 앞 열에 붙인다.   (Wins 행)
//  Pair   : "3.9M","905" 처럼 값 두 개가 한 열.            (Damage 행, "-" 는 단독 열)
Columns MergeColumns(const std::vector<std::wstring>& toks, MergeRule rule)
{
    Columns cols;
    for (const auto& t : toks) {
        bool attach = false;
        if (!cols.empty()) {
            const auto& last = cols.back();
            if (rule == MergeRule::Percent) attach = (!t.empty() && t.back() == L'%');
            else if (rule == MergeRule::Pair) attach = (last.size() == 1 && last[0] != L"-" && t != L"-");
        }
        if (attach) cols.back().push_back(t);
        else cols.push_back({ t });
    }
    return cols;
}

bool ExtractRow(const std::vector<std::wstring>& tk, size_t from, const wchar_t* label,
                MergeRule rule, Columns& out)
{
    for (size_t i = from; i < tk.size(); ++i) {
        if (!EqualsNoCase(tk[i], label)) continue;
        std::vector<std::wstring> vals;
        for (size_t j = i + 1; j < tk.size() && !IsRowLabel(tk[j]); ++j) vals.push_back(tk[j]);
        out = MergeColumns(vals, rule);
        return true;
    }
    return false;
}

// 타일("Recent WNX" + 값)을 찾는다. 값은 같은 토큰 안에 있거나 바로 다음 토큰들 중에 있다.
bool FindTile(const std::vector<std::wstring>& tk, const std::wregex& re, int modeGroup, int valueGroup,
              bool percent, double& val, std::wstring* modeOut)
{
    for (size_t i = 0; i < tk.size(); ++i) {
        std::wsmatch m;
        if (!std::regex_search(tk[i], m, re)) continue;
        double v = 0;
        bool got = false;
        if (valueGroup > 0 && m.size() > (size_t)valueGroup && m[valueGroup].matched && m[valueGroup].length() > 0) {
            std::wstring inl = m[valueGroup].str();
            got = percent ? IsPercent(inl, v) : IsPlainNumber(inl, v);
        }
        for (size_t j = i + 1; !got && j < tk.size() && j <= i + 3; ++j)
            got = percent ? IsPercent(tk[j], v) : IsPlainNumber(tk[j], v);
        if (!got) continue;
        val = v;
        if (modeOut && modeGroup > 0 && m.size() > (size_t)modeGroup && m[modeGroup].matched)
            *modeOut = m[modeGroup].str();
        return true;
    }
    return false;
}

// ── 로마 숫자 (전차 티어) ─────────────────────────────────────
int RomanToInt(const std::wstring& r)
{
    static const std::map<std::wstring, int> m = {
        {L"I",1},{L"II",2},{L"III",3},{L"IV",4},{L"V",5},{L"VI",6},{L"VII",7},{L"VIII",8},{L"IX",9},{L"X",10},{L"XI",11} };
    auto it = m.find(r);
    return it == m.end() ? 0 : it->second;
}

std::vector<std::wstring> JsonStringArray(const nlohmann::json& j)
{
    std::vector<std::wstring> v;
    if (j.is_array()) for (auto& e : j) if (e.is_string()) v.push_back(Trim(FromUtf8(e.get<std::string>())));
    return v;
}

bool HasLetters(const std::wstring& s, size_t minCount)
{
    size_t n = 0;
    for (wchar_t c : s) if (std::iswalpha((wint_t)c) || (c >= 0xAC00 && c <= 0xD7A3)) ++n;
    return n >= minCount;
}

BattleRecord MakeRecord(const nlohmann::json& jr)
{
    BattleRecord b;
    b.lines = JsonStringArray(jr.value("lines", nlohmann::json::array()));
    const auto alts = JsonStringArray(jr.value("alts", nlohmann::json::array()));

    if (jr.contains("win") && jr["win"].is_boolean())
        b.result = jr["win"].get<bool>() ? BattleResult::Win : BattleResult::Loss;

    static const std::wregex reRoman(L"^(XI|X|IX|VIII|VII|VI|V|IV|III|II|I)$");
    static const std::wregex reRomanTank(L"^(XI|X|IX|VIII|VII|VI|V|IV|III|II|I)\\s+(\\S.*)$");
    static const std::wregex reLabelVal(L"^WN(?:X|8)\\s+(\\d[\\d,]*)$", std::regex::icase);

    size_t tierLine = std::wstring::npos;

    for (size_t i = 0; i < b.lines.size(); ++i) {
        const std::wstring& ln = b.lines[i];
        std::wsmatch m;
        double v = 0;

        // 지표값: "WNX" 다음 줄, "WNX 3717" 한 줄, 또는 값 다음에 라벨
        if (!b.hasWnx) {
            if (EqualsNoCase(ln, L"WNX") || EqualsNoCase(ln, L"WN8")) {
                if (i + 1 < b.lines.size() && IsPlainNumber(b.lines[i + 1], v)) { b.hasWnx = true; b.wnx = v; }
                else if (i > 0 && IsPlainNumber(b.lines[i - 1], v))              { b.hasWnx = true; b.wnx = v; }
            } else if (std::regex_match(ln, m, reLabelVal)) {
                IsPlainNumber(m[1].str(), v); b.hasWnx = true; b.wnx = v;
            }
        }

        // 티어 + 전차명
        if (b.tier == 0) {
            if (std::regex_match(ln, m, reRoman)) {
                b.tier = RomanToInt(ln);
                tierLine = i;
                for (size_t j = i + 1; j < b.lines.size() && j <= i + 2; ++j) {
                    double dummy;
                    if (HasLetters(b.lines[j], 2) && !IsPlainNumber(b.lines[j], dummy) &&
                        !EqualsNoCase(b.lines[j], L"WNX") && !EqualsNoCase(b.lines[j], L"WN8")) {
                        b.tankName = b.lines[j];
                        break;
                    }
                }
            } else if (std::regex_match(ln, m, reRomanTank)) {
                b.tier = RomanToInt(m[1].str());
                b.tankName = m[2].str();
                tierLine = i;
            }
        }
    }

    // 전차명을 못 찾았으면 이미지 alt 에서 보충
    if (b.tankName.empty()) for (auto& a : alts) if (HasLetters(a, 2)) { b.tankName = a; break; }

    // 맵 이름(휴리스틱): 티어 줄보다 앞에 있는 첫 번째 글자 줄
    size_t limit = (tierLine == std::wstring::npos) ? (std::min)(b.lines.size(), (size_t)3) : tierLine;
    for (size_t i = 0; i < limit; ++i)
        if (HasLetters(b.lines[i], 3) && b.lines[i] != b.tankName) { b.mapName = b.lines[i]; break; }

    // ── 구조화된 값 (BattleRow DOM 에서 JS 가 뽑아 준 값) 이 있으면 위 텍스트 추정보다 우선한다 ──
    auto str = [&](const char* key) -> std::wstring {
        return (jr.contains(key) && jr[key].is_string()) ? Trim(FromUtf8(jr[key].get<std::string>())) : std::wstring();
    };

    if (const std::wstring v = ToLower(str("res")); !v.empty()) {          // <div result="win|loss">
        if (v == L"win") b.result = BattleResult::Win;
        else if (v == L"loss" || v == L"lose" || v == L"defeat") b.result = BattleResult::Loss;
        else b.result = BattleResult::Unknown;
    }
    if (const std::wstring v = str("tank"); !v.empty()) b.tankName = v;
    if (const std::wstring v = str("map");  !v.empty()) b.mapName = v;
    if (const std::wstring v = str("tier"); !v.empty()) { int t = RomanToInt(v); if (t > 0) b.tier = t; }
    {
        double v = 0;
        if (IsPlainNumber(str("rating"), v)) { b.hasWnx = true; b.wnx = v; }
    }

    // 차량 종류 아이콘(/classIcons/SPG.svg, alt="SPG") → 자주포 여부
    b.isArty = EqualsNoCase(str("cls"), L"SPG") || EqualsNoCase(str("clsSrc"), L"SPG");

    if (jr.contains("data") && jr["data"].is_object()) {
        const auto& d = jr["data"];
        auto num = [&](const char* key, double& out) -> bool {
            return d.contains(key) && d[key].is_string() &&
                   IsPlainNumber(Trim(FromUtf8(d[key].get<std::string>())), out);
        };
        double dmg = 0, ast = 0;
        if (num("damage", dmg) && num("assist", ast)) {
            b.hasDetail = true;
            b.damage = (int)dmg;
            b.assist = (int)ast;
        }
        {
            double v = 0;                      // 격파는 자주포도 의미가 있으므로 항상 읽는다
            if (num("frags", v)) { b.hasFrags = true; b.frags = (int)v; }
        }
        if (!b.isArty) {                       // 자주포: 발견/도탄 칸은 아예 읽지 않는다
            double v = 0;
            if (num("spots", v))   b.spotted = (int)v;
            if (num("blocked", v)) b.bounces = (int)v;
        }
        if (d.contains("accuracy") && d["accuracy"].is_string()) {
            static const std::wregex reShots(L"^(\\d+)\\s*/\\s*(\\d+)\\s*/\\s*(\\d+)$");
            std::wsmatch sm;
            const std::wstring acc = Trim(FromUtf8(d["accuracy"].get<std::string>()));
            if (std::regex_match(acc, sm, reShots)) {
                b.hasShots = true;
                b.shots = (int)std::wcstol(sm[1].str().c_str(), nullptr, 10);
                b.hits  = (int)std::wcstol(sm[2].str().c_str(), nullptr, 10);
                b.pens  = (int)std::wcstol(sm[3].str().c_str(), nullptr, 10);
            }
        }
    }

    return b;
}

} // namespace

// ═════════════════════════════════════════════════════════════
UrlInfo CWnxParser::ClassifyUrl(const std::wstring& url)
{
    UrlInfo u;
    // 실제 주소 형태 (서버가 닉네임-ID 뒤에 온다):
    static const std::wregex re(
        L"^https?://(?:www\\.)?tomato\\.gg/stats/([A-Za-z0-9_]+)-(\\d+)/([A-Za-z]+)(/[^?#]*)?(?:\\?([^#]*))?(?:#.*)?$",
        std::regex::icase);
    std::wsmatch m;
    if (!std::regex_match(url, m, re)) return u;

    u.nick      = m[1].str();
    u.accountId = m[2].str();
    u.server    = m[3].str();

    // 서버 뒤에 경로가 더 붙으면(예: /ASIA/rankings) 다른 탭으로 본다.
    std::wstring extraPath = m[4].matched ? m[4].str() : L"";
    extraPath.erase(std::remove(extraPath.begin(), extraPath.end(), L'/'), extraPath.end());

    // 쿼리의 tab=... 인자. Main 탭에서는 이 인자가 아예 없다.
    std::wstring tab;
    if (m[5].matched) {
        const std::wstring q = m[5].str();
        size_t pos = 0;
        while (pos <= q.size()) {
            size_t amp = q.find(L'&', pos);
            if (amp == std::wstring::npos) amp = q.size();
            const std::wstring kv = q.substr(pos, amp - pos);
            const size_t eq = kv.find(L'=');
            if (eq != std::wstring::npos && EqualsNoCase(kv.substr(0, eq), L"tab")) tab = kv.substr(eq + 1);
            pos = amp + 1;
        }
    }

    u.subPath = !extraPath.empty() ? extraPath : tab;
    const bool isMain = extraPath.empty() && (tab.empty() || EqualsNoCase(tab, L"main"));
    u.tab = isMain ? TabKind::Main : TabKind::Other;
    return u;
}

bool CWnxParser::ParseOverview(const std::wstring& pageText, const std::wstring& nick,
                               OverviewData& out, std::wstring* err)
{
    auto fail = [&](const wchar_t* msg) { if (err) *err = msg; return false; };

    // 이전 페이지 DOM 이 남아있는 경우 방어
    if (ToLower(pageText).find(ToLower(nick)) == std::wstring::npos) return fail(L"닉네임이 페이지에 없음");

    const auto tk = Tokenize(pageText);
    OverviewData ov;

    // 1) 상단 타일
    static const std::wregex reRecentR(L"^Recent\\s*(WNX|WN8)\\s*(\\d[\\d,]*)?", std::regex::icase);
    static const std::wregex reOverallR(L"^Overall\\s*(WNX|WN8)\\s*(\\d[\\d,]*)?", std::regex::icase);
    static const std::wregex reRecentW(L"^Recent\\s*Win\\s*rate\\s*([0-9.]+%)?", std::regex::icase);
    static const std::wregex reOverallW(L"^Overall\\s*Win\\s*rate\\s*([0-9.]+%)?", std::regex::icase);

    std::wstring modeStr;
    if (!FindTile(tk, reRecentR, 1, 2, false, ov.recentWnx, &modeStr))  return fail(L"Recent 지표 타일 없음");
    if (!FindTile(tk, reOverallR, 1, 2, false, ov.overallWnx, nullptr)) return fail(L"Overall 지표 타일 없음");
    ov.mode = EqualsNoCase(modeStr, L"WN8") ? RatingMode::Wn8 : RatingMode::Wnx;
    bool gotRW = FindTile(tk, reRecentW, 0, 1, true, ov.recentWinrate, nullptr);
    bool gotOW = FindTile(tk, reOverallW, 0, 1, true, ov.overallWinrate, nullptr);

    // 2) Stats Overview 표
    size_t start = 0;
    for (size_t i = 0; i < tk.size(); ++i) if (EqualsNoCase(tk[i], L"Stats Overview")) { start = i; break; }
    size_t h = std::wstring::npos;
    for (size_t i = start; i < tk.size(); ++i) if (EqualsNoCase(tk[i], L"Overall")) { h = i; break; }
    if (h == std::wstring::npos) return fail(L"기간 표 헤더 없음");

    std::vector<std::wstring> header;
    size_t hend = h;
    while (hend < tk.size() && IsPeriodLabel(tk[hend])) header.push_back(tk[hend++]);
    if (header.size() < 2) return fail(L"기간 표 헤더 열 부족");

    Columns battles, tier, rating, wins, dmg;
    if (!ExtractRow(tk, hend, L"Battles", MergeRule::None, battles))  return fail(L"Battles 행 없음");
    if (battles.size() != header.size())                              return fail(L"Battles 열 개수 불일치");
    ExtractRow(tk, hend, L"Avg Tier", MergeRule::None, tier);
    if (!ExtractRow(tk, hend, RatingLabel(ov.mode), MergeRule::None, rating))
        ExtractRow(tk, hend, ov.mode == RatingMode::Wnx ? L"WN8" : L"WNX", MergeRule::None, rating);
    ExtractRow(tk, hend, L"Wins", MergeRule::Percent, wins);
    ExtractRow(tk, hend, L"Damage", MergeRule::Pair, dmg);

    for (size_t c = 0; c < header.size(); ++c) {
        PeriodStat p;
        p.label = header[c];
        double v = 0;
        if (c < battles.size() && IsPlainNumber(battles[c][0], v)) p.battles = (int)v;
        if (c < tier.size()    && IsPlainNumber(tier[c][0], v))    p.avgTier = v;
        bool hasRating = (c < rating.size() && IsPlainNumber(rating[c][0], v));
        if (hasRating) p.wnx = v;
        if (c < wins.size() && wins[c].size() >= 2 && IsPercent(wins[c][1], v)) { p.winrate = v; p.hasWinrate = true; }
        if (c < dmg.size()  && dmg[c].size()  >= 2 && IsPlainNumber(dmg[c][1], v)) { p.avgDamage = v; p.hasDamage = true; }
        p.hasData = (p.battles > 0 && hasRating);
        ov.periods.push_back(std::move(p));
    }

    // 승률 타일을 못 찾았으면 표에서 보충
    if (!gotOW && !ov.periods.empty() && ov.periods[0].hasWinrate) ov.overallWinrate = ov.periods[0].winrate;
    if (!gotRW) for (auto& p : ov.periods) if (EqualsNoCase(p.label, L"1000 Battles") && p.hasWinrate) ov.recentWinrate = p.winrate;

    out = std::move(ov);
    return true;
}

bool CWnxParser::ParseBattles(const std::string& jsonUtf8, std::vector<BattleRecord>& out, std::wstring* err)
{
    out.clear();
    auto fail = [&](const wchar_t* msg) { if (err) *err = msg; return false; };
    if (jsonUtf8.empty()) return fail(L"빈 응답");

    try {
        nlohmann::json j = nlohmann::json::parse(jsonUtf8);
        if (!j.is_object())                 return fail(L"JSON 객체가 아님");
        if (!j.value("header", false))      return fail(L"Battles 패널 제목을 못 찾음");
        if (!j.value("ok", false))          return fail(L"Battles 수집 실패");
        if (!j.contains("rows") || !j["rows"].is_array()) return fail(L"rows 없음");

        for (auto& jr : j["rows"]) out.push_back(MakeRecord(jr));
    } catch (...) {
        out.clear();
        return fail(L"JSON 파싱 예외");
    }

    // 절반 이상이 티어(로마 숫자)를 가지고 있으면, 티어 없는 행(요약 블록 등)은 전투 행이 아니라고 본다.
    size_t withTier = 0;
    for (auto& b : out) if (b.tier > 0) ++withTier;
    if (!out.empty() && withTier * 2 >= out.size())
        out.erase(std::remove_if(out.begin(), out.end(), [](const BattleRecord& b) { return b.tier == 0; }), out.end());

    return true;
}
