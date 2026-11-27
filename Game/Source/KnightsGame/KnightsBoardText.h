#pragma once

// 보드 화면 문구 만들기. 엔진과 무관한 문자열 함수만 둔다.

#include "KnightsGame/KnightsRules.h"

#include <string>

namespace Knights
{
    // "나", "AI 1" ~ "AI 4"
    std::string PlayerName(int player);

    // AI 성향 이름("공격형" 등). 사람(0)은 빈 문자열.
    std::string PersonalityName(int player);

    // 로그·오류 표시용 이름("DuplicateRegion" 등)
    const char* SubmitErrorName(SubmitError error);

    enum class EventTone { Nothing, Win, Wipe, Reinforce };

    // 정산 이벤트 하나를 화면에 보여 줄 문구.
    struct EventText
    {
        std::string headline;  // 하단 진행 문구(예: "트롤 다리 승리! 나 +2, AI 3 +5")
        std::string detail;    // 하단 보조 문구(왜 그렇게 되었는지)
        std::string cardLine;  // 지역 카드 아래에 남겨 두는 짧은 결과
        EventTone tone = EventTone::Nothing;
    };

    EventText DescribeEvent(const RegionEvent& event);

    // 진행 기록 창에 보여 줄 라운드 하나.
    struct RoundLogText
    {
        std::string title;  // "라운드 3"
        std::string body;   // 여러 줄: 몬스터·주사위, 배치, 정산, 라운드 끝 점수
    };

    RoundLogText DescribeRound(const RoundRecord& record);
}
