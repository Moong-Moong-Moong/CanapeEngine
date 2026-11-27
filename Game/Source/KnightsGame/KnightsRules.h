#pragma once

// 기사단 게임 룰 코어. 표준 라이브러리만 사용하며 엔진에 의존하지 않는다.

#include <array>
#include <cstdint>
#include <random>
#include <string_view>
#include <vector>

namespace Knights
{
    constexpr int kPlayerCount = 5;
    constexpr int kRegionCount = 6;
    constexpr int kRoundCount = 8;
    constexpr int kFlagsPerPlayer = 2;
    constexpr int kStartKnights = 5;
    constexpr int kHumanPlayer = 0;

    enum class RegionKind { Monster, TrainingGround, MercenaryMarket };

    // 증원 지역은 minScore, maxScore가 0이다.
    struct RegionDef
    {
        std::string_view name;
        RegionKind kind;
        int minScore;
        int maxScore;
    };

    const std::array<RegionDef, kRegionCount>& GetRegionDefs();

    // 공개는 단계 전환으로 표현한다: KnightPlacement 진입 = 깃발 공개, Resolution 진입 = 기사 공개 + 정산 완료.
    enum class Phase { FlagPlacement, KnightPlacement, Resolution, GameOver };

    enum class SubmitError
    {
        None,
        WrongPhase,
        AlreadySubmitted,
        InvalidPlayer,
        InvalidRegion,
        DuplicateRegion,
        NoFlagThere,
        NotMonsterRegion,
        NegativeCount,
        ExceedsKnights
    };

    using FlagChoice = std::array<int, kFlagsPerPlayer>;     // 지역 인덱스 2개
    using KnightAllocation = std::array<int, kRegionCount>;  // 지역별 보낸 기사 수

    struct PlayerState
    {
        int score = 0;
        int knights = kStartKnights;
    };

    // 지역 하나의 정산 결과.
    struct RegionEvent
    {
        int region = 0;
        RegionKind kind = RegionKind::Monster;
        int monsterScore = 0;
        int totalKnights = 0;
        bool wiped = false;
        int dice = 0;
        int discarded = 0;
        std::array<int, kPlayerCount> sent{};
        std::array<bool, kPlayerCount> flagged{};
        std::array<int, kPlayerCount> scoreGained{};
        std::array<int, kPlayerCount> knightsLost{};
        std::array<int, kPlayerCount> knightsGained{};
    };

    // 순수 함수: 몬스터 지역 정산 규칙만. sent는 깃발이 있는 플레이어만 0보다 크다고 가정한다.
    RegionEvent ResolveMonster(int region, int monsterScore, const std::array<int, kPlayerCount>& sent,
                               const std::array<bool, kPlayerCount>& flagged);

    // 훈련소: 깃발 1개당 기사 +1.
    RegionEvent ResolveTraining(int region, const std::array<bool, kPlayerCount>& flagged);

    // 용병 시장: 주사위 눈 dice를 깃발 인원 n명이 floor(dice/n)씩 나누고 나머지는 폐기한다. n이 0이면 아무 일도 없다.
    RegionEvent ResolveMarket(int region, int dice, const std::array<bool, kPlayerCount>& flagged);

    struct PublicView
    {
        int viewer = 0;
        int round = 1;  // 1..8
        Phase phase = Phase::FlagPlacement;
        std::array<PlayerState, kPlayerCount> players{};
        std::array<int, kRegionCount> monsterScores{};  // 증원 지역은 0
        std::array<bool, kPlayerCount> submitted{};     // 현재 단계 제출 여부(모두 공개)
        std::array<std::array<bool, kRegionCount>, kPlayerCount> flags{};  // 공개 전에는 viewer 것만 채워짐
        std::array<KnightAllocation, kPlayerCount> knights{};              // 공개 전에는 viewer 것만 채워짐
    };

    // 최고 점수, 동점이면 기사가 많은 쪽, 그래도 같으면 모두. 인덱스 오름차순.
    std::vector<int> RankWinners(const std::array<PlayerState, kPlayerCount>& players);

    class Game
    {
    public:
        explicit Game(uint32_t seed);

        // 1라운드 FlagPlacement로 되돌리고 몬스터 점수를 굴린다.
        void Reset(uint32_t seed);

        Phase GetPhase() const { return m_phase; }
        int GetRound() const { return m_round; }
        const PlayerState& GetPlayer(int player) const { return m_players[player]; }
        int GetMonsterScore(int region) const { return m_monsterScores[region]; }
        bool HasSubmitted(int player) const { return m_submitted[player]; }

        SubmitError SubmitFlags(int player, const FlagChoice& regions);
        SubmitError SubmitKnights(int player, const KnightAllocation& allocation);
        // Resolution 단계의 정산 기록(지역 0~5 순서). 다른 단계에서는 비어 있다.
        const std::vector<RegionEvent>& GetResolution() const { return m_resolution; }
        void AdvanceRound();
        std::vector<int> GetWinners() const;
        PublicView GetView(int viewer) const;

    private:
        void RollMonsterScores();

        std::mt19937 m_rng;
        int m_round = 1;
        Phase m_phase = Phase::FlagPlacement;
        std::array<PlayerState, kPlayerCount> m_players{};
        std::array<int, kRegionCount> m_monsterScores{};
        std::array<bool, kPlayerCount> m_submitted{};
        std::array<std::array<bool, kRegionCount>, kPlayerCount> m_flags{};
        std::array<KnightAllocation, kPlayerCount> m_knights{};
        std::vector<RegionEvent> m_resolution;
    };
}
