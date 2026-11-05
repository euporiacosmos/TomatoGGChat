#pragma once
#include <string>
#include <vector>

// ── 지표 모드 ─────────────────────────────────────────────────
// 기본은 WNX. 사이트의 WNX|WN8 탭 전환에 맞춰 바뀝니다.
enum class RatingMode { Wnx, Wn8 };
inline const wchar_t* RatingLabel(RatingMode m) { return m == RatingMode::Wnx ? L"WNX" : L"WN8"; }

// ── 현재 URL 분류 ─────────────────────────────────────────────
enum class TabKind { NotPlayer, Main, Other };

struct UrlInfo {
    TabKind      tab = TabKind::NotPlayer;
    std::wstring server, nick, accountId, subPath;
    bool IsPlayer() const { return tab != TabKind::NotPlayer; }
};

// ── Stats Overview 표의 한 열(기간) ───────────────────────────
// 필드명이 wnx 여도, 앱이 WN8 모드일 때는 WN8 값이 들어갑니다.
struct PeriodStat {
    std::wstring label;               // "Overall", "3 Days", "1000 Battles" ...
    bool   hasData   = false;         // 판수>0 이고 지표값이 있을 때 true
    int    battles   = 0;
    double avgTier   = 0;
    double wnx       = 0;
    double winrate   = 0;  bool hasWinrate = false;
    double avgDamage = 0;  bool hasDamage  = false;
};

struct OverviewData {
    RatingMode mode = RatingMode::Wnx;
    double recentWnx = 0, overallWnx = 0;
    double recentWinrate = 0, overallWinrate = 0;
    std::vector<PeriodStat> periods;  // [0] = Overall
};

// ── Battles 목록의 한 줄 (최신순) ─────────────────────────────
enum class BattleResult { Unknown, Win, Loss };

struct BattleRecord {
    std::wstring mapName;             // 휴리스틱(검증 전) - 프롬프트에는 쓰지 않음
    std::wstring tankName;
    int          tier = 0;
    BattleResult result = BattleResult::Unknown;
    bool         hasWnx = false;
    double       wnx = 0;

    // 행 안의 아이콘 옆 수치 (아이콘 alt 이름 damage / assist / spots / blocked / accuracy 로 읽는다)
    bool         isArty = false;  // 차량 종류 아이콘이 SPG(자주포)면 true → 발견/도탄은 읽지도, 쓰지도 않는다
    bool         hasDetail = false;   // 딜/도움딜을 읽었을 때 true
    int          damage  = 0;     // 딜
    int          assist  = 0;     // 도움딜
    bool         hasFrags = false;
    int          frags   = 0;     // 격파 (frags 아이콘 칸)
    int          spotted = 0;     // 발견 (isArty 면 읽지 않음)
    int          bounces = 0;     // 도탄 = 사이트의 blocked 칸 (isArty 면 읽지 않음)
    bool         hasShots = false;
    int          shots = 0;       // 발사 수   ┐ accuracy 칸 "13/1/0"
    int          hits  = 0;       // 명중 수   │ → "13발 중 1발 명중"
    int          pens  = 0;       // 관통 수(세 번째 값, 프롬프트에는 쓰지 않음) ┘

    std::vector<std::wstring> lines;  // 원본 텍스트 줄 (디버그용 / 구조화 값이 없을 때의 예비 파싱용)
};

struct PlayerData {
    std::wstring server, nick, accountId;
    OverviewData overview;
    std::vector<BattleRecord> battles;
    bool overviewReady = false;
    bool battlesReady  = false;
    bool scrapeComplete = false;
};

struct ChatMsg { std::string role, content; };   // UTF-8. role: system / user / assistant
