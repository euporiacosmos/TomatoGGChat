#pragma once
#include <string>
#include <vector>
#include "Models.h"

// Tomato.gg 페이지 텍스트/DOM 추출 결과를 구조체로 바꾸는 파서 (WNX 기준)
class CWnxParser
{
public:
    // URL → 서버/닉네임/계정ID/탭 종류.
    // ★ 메인 탭 URL 규칙이 다르면 이 함수만 고치세요.
    static UrlInfo ClassifyUrl(const std::wstring& url);

    // document.body.innerText → Overview(타일 + 기간별 표)
    static bool ParseOverview(const std::wstring& pageText, const std::wstring& nick,
        OverviewData& out, std::wstring* err = nullptr);

    // ReadBattles() 스크립트의 JSON(UTF-8) → 전투 목록 (최신순 유지)
    static bool ParseBattles(const std::string& jsonUtf8, std::vector<BattleRecord>& out,
        std::wstring* err = nullptr);
};
