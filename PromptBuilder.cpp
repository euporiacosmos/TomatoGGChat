#include "pch.h"
#include "PromptBuilder.h"
#include "Util.h"
#include <format>
#include <map>

namespace {

    // WN8 의 널리 쓰이는 구간표. (WNX 는 공식 구간표를 확인하기 전까지 등급을 붙이지 않는다.)
    const char* Wn8Grade(double w)
    {
        if (w < 300)  return "매우 나쁨";
        if (w < 450)  return "나쁨";
        if (w < 650)  return "평균 이하";
        if (w < 900)  return "평균";
        if (w < 1200) return "평균 이상";
        if (w < 1600) return "좋음";
        if (w < 2000) return "매우 좋음";
        if (w < 2450) return "훌륭함";
        if (w < 2900) return "유니컴";
        return "슈퍼 유니컴";
    }

    // 전투 한 판의 아이콘 옆 숫자들을 "숫자+이름" 텍스트로 만든다. (숫자만 덩그러니 주지 않기 위해)
    //   예) "571딜, 179도움딜, 0발견, 0도탄, 13발 중 1발 명중"
    std::string BattleDetailText(const BattleRecord& b)
    {
        if (!b.hasDetail) return std::string();
        std::string t = std::format("{}딜", b.damage);
        if (b.hasFrags) t += std::format(", {}격파", b.frags);
        t += std::format(", {}도움딜", b.assist);
        if (!b.isArty) t += std::format(", {}발견, {}도탄", b.spotted, b.bounces);   // 자주포는 발견/도탄을 쓰지 않는다
        if (b.hasShots) t += std::format(", {}발 중 {}발 명중", b.shots, b.hits);
        return t;
    }

} // namespace

std::string CPromptBuilder::BuildStatsBlock(const PlayerData& d, const AppConfig& cfg)
{
    const OverviewData& ov = d.overview;
    const std::string lbl = ToUtf8(RatingLabel(ov.mode));
    const bool wn8 = (ov.mode == RatingMode::Wn8);
    auto grade = [&](double v) -> std::string {
        return wn8 ? std::format(" [{}]", Wn8Grade(v)) : std::string();
        };

    std::string s;
    s += std::format("- 닉네임: {} ({})\n", ToUtf8(d.nick), ToUtf8(d.server));
    s += std::format("- 사용 지표: {}\n", lbl);
    s += std::format("- 최근 {0}: {1:.0f}{2} / 전체 {0}: {3:.0f}{4} (차이 {5:+.0f})\n",
        lbl, ov.recentWnx, grade(ov.recentWnx), ov.overallWnx, grade(ov.overallWnx),
        ov.recentWnx - ov.overallWnx);
    s += std::format("- 최근 승률: {:.2f}% / 전체 승률: {:.2f}% (차이 {:+.2f}%p)\n",
        ov.recentWinrate, ov.overallWinrate, ov.recentWinrate - ov.overallWinrate);

    // 기간별 표
    s += "- 기간별 (판수가 적으면 신뢰도가 낮음):\n";
    for (const auto& p : ov.periods) {
        if (!p.hasData || EqualsNoCase(p.label, L"Overall")) continue;
        std::string line = std::format("  · {}: {}판, {} {:.0f}", ToUtf8(p.label), p.battles, lbl, p.wnx);
        if (p.hasWinrate) line += std::format(", 승률 {:.2f}%", p.winrate);
        if (p.avgTier > 0) line += std::format(", 평균티어 {:.2f}", p.avgTier);
        if (p.hasDamage)  line += std::format(", 평균피해 {:.0f}", p.avgDamage);
        s += line + "\n";
    }
    if (!ov.periods.empty() && ov.periods[0].hasData) {
        const auto& p = ov.periods[0];
        s += std::format("  · 전체: {}판, 평균티어 {:.2f}", p.battles, p.avgTier);
        if (p.hasDamage) s += std::format(", 평균피해 {:.0f}", p.avgDamage);
        s += "\n";
    }

    // 최근 전투 요약
    const size_t n = (std::min)(d.battles.size(), (size_t)(std::max)(cfg.statBattles, 1));
    if (n == 0) {
        s += "- 최근 전투 목록: 수집하지 못함 (위 요약 지표만 근거로 말할 것)\n";
        return s;
    }

    int wins = 0, losses = 0, unknown = 0, rated = 0;
    double sum = 0;
    const BattleRecord* best = nullptr, * worst = nullptr;
    std::map<std::wstring, int> tankCount;
    for (size_t i = 0; i < n; ++i) {
        const auto& b = d.battles[i];
        if (b.result == BattleResult::Win) ++wins;
        else if (b.result == BattleResult::Loss) ++losses;
        else ++unknown;
        if (!b.tankName.empty()) ++tankCount[b.tankName];
        if (b.hasWnx) {
            ++rated; sum += b.wnx;
            if (!best || b.wnx > best->wnx)  best = &b;
            if (!worst || b.wnx < worst->wnx) worst = &b;
        }
    }

    s += std::format("- 가장 최근 {}판 요약 (최신순):\n", n);
    {
        s += std::format("  · {} 승리 / {} 패배 {}\n", wins, losses, unknown ? std::format(" / {} 무승부", unknown) : std::string());
    }
    if (rated > 0) {
        s += std::format("  · 판당 평균 {} {:.0f}\n", lbl, sum / rated);
        if (best)  s += std::format("  · 최고: {} {:.0f}\n",
            best->tankName.empty() ? std::string("전차 미상") : ToUtf8(best->tankName), best->wnx);
        if (worst) s += std::format("  · 최저: {} {:.0f}\n",
            worst->tankName.empty() ? std::string("전차 미상") : ToUtf8(worst->tankName), worst->wnx);
    }
    // 많이 탄 전차 Top3
    std::vector<std::pair<std::wstring, int>> tc(tankCount.begin(), tankCount.end());
    std::sort(tc.begin(), tc.end(), [](auto& x, auto& y) { return x.second > y.second; });
    if (!tc.empty()) {
        s += "  · 자주 탄 전차: ";
        for (size_t i = 0; i < tc.size() && i < 3; ++i)
            s += std::format("{}{}({}판)", i ? ", " : "", ToUtf8(tc[i].first), tc[i].second);
        s += "\n";
    }
    // 개별 전투 나열
    const size_t listN = (std::min)(d.battles.size(), (size_t)(std::max)(cfg.promptBattles, 0));
    if (listN > 0) {
        s += "- 최근 전투 목록 (위에서부터 가장 최근):\n";
        for (size_t i = 0; i < listN; ++i) {
            const auto& b = d.battles[i];
            std::string tank = b.tankName.empty() ? std::string("전차 미상") : ToUtf8(b.tankName);
            std::string tier = b.tier > 0 ? std::format(" {}티어", b.tier) : std::string();
            std::string res = b.result == BattleResult::Win ? " 승리" : b.result == BattleResult::Loss ? " 패배" : " 무승부";
            std::string r = b.hasWnx ? std::format(" {} {:.0f}", lbl, b.wnx) : std::string();
            const std::string detail = BattleDetailText(b);
            s += std::format("  {}. {}{}{}{}{}\n", i + 1, tank, tier, res, r,
                detail.empty() ? std::string() : " (" + detail + ")");
        }
    }
    return s;
}

std::string CPromptBuilder::BuildSystem(const PlayerData& d, const AppConfig& cfg)
{
    const bool wn8 = (d.overview.mode == RatingMode::Wn8);

    std::string sys =
        "너는 월드 오브 탱크를 오래 한 입담 좋은 전차장이다. 친근한 존댓말로, 약간 장난스럽게 말하되 "
        "선을 넘는 비하는 하지 않고, 반드시 자연스러운 한국어로만 답변할 것.\n"
        "규칙:\n"
        "1) 아래 [전적]에 있는 숫자와 전차 이름만 사용한다. 없는 숫자, 전차, 맵은 절대 지어내지 않는다.\n";
    sys += wn8
        ? "2) 등급 표현은 [전적]에 괄호로 적힌 것만 쓰고, 다른 등급을 만들어내지 않는다.\n"
        : "2) WNX 는 주 지표다. 등급이나 구간표를 만들어내지 말고, 최근/전체/기간별 값을 서로 비교해서만 평가한다.\n";
    sys +=
        "3) 판수가 적은 기간이나 표본은 신뢰도가 낮다고 말한다.\n"
        "4) 분석 요청에는 5~7문장으로: 한 줄 총평 → 최근 흐름 → 칭찬 1개 → 훈수 1~2개 순서로 쓴다.\n"
        "5) 질문에는 [전적]을 근거로 짧게 답하고, 근거가 없으면 모른다고 솔직히 말한다.\n"
        "[전적]\n";
    sys += BuildStatsBlock(d, cfg);
    return sys;
}

std::string CPromptBuilder::BuildAnalysisRequest()
{
    return "위 [전적]을 바탕으로 이 플레이어의 최근 전투 분석 리포트를 써줘.";
}
