#pragma once

// 기사단 게임 AI. 입력은 공개 뷰(PublicView)뿐이므로 숨긴 정보(남의 미공개 깃발·배치)에 접근할 길이 없다.
// 표준 라이브러리만 사용하며 엔진에 의존하지 않는다.

#include "KnightsGame/KnightsRules.h"

#include <cstdint>
#include <random>

namespace Knights
{
    enum class AiPersonality { Aggressive, Cautious, Reinforcer, Balanced };

    // 플레이어 1 공격형, 2 신중형, 3 증원형, 4 균형형. 그 밖의 인덱스는 균형형.
    AiPersonality GetAiPersonality(int player);

    class Ai
    {
    public:
        Ai(AiPersonality personality, uint32_t seed);

        AiPersonality GetPersonality() const { return m_personality; }

        // 항상 서로 다른 유효 지역 2개(오름차순).
        FlagChoice ChooseFlags(const PublicView& view);

        // 항상 SubmitKnights가 None을 돌려줄 배치: 내 깃발이 있는 몬스터 지역에만, 합은 보유 기사 이하.
        KnightAllocation ChooseKnights(const PublicView& view);

    private:
        AiPersonality m_personality;
        std::mt19937 m_rng;
    };
}
