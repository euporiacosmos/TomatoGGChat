#pragma once
#include <string>
#include <vector>
#include "Models.h"
#include "Config.h"

// 사실 계산(차이, 승패 집계, 최고/최저 전투)은 여기서 C++ 로 끝내고,
// 소형 모델(SLM)은 말투와 훈수만 담당하게 한다.
class CPromptBuilder
{
public:
    // system 메시지 = 페르소나 + 규칙 + [전적] 블록
    static std::string BuildSystem(const PlayerData& d, const AppConfig& cfg);
    // 자동 분석용 user 메시지
    static std::string BuildAnalysisRequest();
    // [전적] 블록만 (디버그/덤프용)
    static std::string BuildStatsBlock(const PlayerData& d, const AppConfig& cfg);
};
