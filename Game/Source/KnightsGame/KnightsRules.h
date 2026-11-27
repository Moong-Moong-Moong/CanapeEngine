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
    constexpr int kLateRoundStart = 5;         // 이 라운드부터 몬스터 점수 범위가 오른다
    constexpr int kLateRoundMonsterBonus = 2;  // 그때 최소·최댓값에 더하는 값
    constexpr int kMaxMonsterScore = 17;       // 나올 수 있는 최대 몬스터 점수(드래곤 15 + 후반 2)

    enum class RegionKind { Monster, TrainingGround, MercenaryMarket };

    // 1~4라운드 기준 범위. 증원 지역은 minScore, maxScore가 0이다.
    struct RegionDef
    {
        std::string_view name;
        RegionKind kind;
        int minScore;
        int maxScore;
    };

    const std::array<RegionDef, kRegionCount>& GetRegionDefs();

    struct ScoreRange
    {
        int min;
        int max;
    };

    // 해당 라운드에 실제로 쓰는 몬스터 점수 범위(kLateRoundStart부터 보정). 증원 지역은 {0, 0}.
    ScoreRange GetMonsterRange(int region, int round);

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
        int marketDice = 0;                             // 이번 라운드 용병 시장 주사위(1~6, 라운드 시작에 공개)
        std::array<bool, kPlayerCount> submitted{};     // 현재 단계 제출 여부(모두 공개)
        std::array<std::array<bool, kRegionCount>, kPlayerCount> flags{};  // 공개 전에는 viewer 것만 채워짐
        std::array<KnightAllocation, kPlayerCount> knights{};              // 공개 전에는 viewer 것만 채워짐
    };

    // 정산이 끝난 라운드 하나의 기록. 모든 정보가 공개된 뒤에만 만들어지므로 누구에게 보여 줘도 된다.
    struct RoundRecord
    {
        int round = 1;
        std::array<int, kRegionCount> monsterScores{};
        int marketDice = 0;
        std::array<std::array<bool, kRegionCount>, kPlayerCount> flags{};
        std::array<KnightAllocation, kPlayerCount> knights{};
        std::vector<RegionEvent> events;                 // 지역 0~5 순서
        std::array<PlayerState, kPlayerCount> playersAfter{};  // 정산 뒤 점수·보유 기사
    };

    // 최고 점수, 동점이면 기사가 많은 쪽, 그래도 같으면 모두. 인덱스 오름차순.
    std::vector<int> RankWinners(const std::array<PlayerState, kPlayerCount>& players);

    class Game
    {
    public:
        explicit Game(uint32_t seed);

        // 1라운드 FlagPlacement로 되돌리고 몬스터 점수와 용병 시장 주사위를 굴린다.
        void Reset(uint32_t seed);

        Phase GetPhase() const { return m_phase; }
        int GetRound() const { return m_round; }
        const PlayerState& GetPlayer(int player) const { return m_players[player]; }
        int GetMonsterScore(int region) const { return m_monsterScores[region]; }
        int GetMarketDice() const { return m_marketDice; }
        bool HasSubmitted(int player) const { return m_submitted[player]; }

        SubmitError SubmitFlags(int player, const FlagChoice& regions);
        SubmitError SubmitKnights(int player, const KnightAllocation& allocation);
        // Resolution 단계의 정산 기록(지역 0~5 순서). 다른 단계에서는 비어 있다.
        const std::vector<RegionEvent>& GetResolution() const { return m_resolution; }
        // 정산이 끝난 라운드 기록(오래된 순). 정산되는 순간 그 라운드가 추가되고, Reset에서 비운다.
        const std::vector<RoundRecord>& GetHistory() const { return m_history; }
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
        int m_marketDice = 0;
        std::array<bool, kPlayerCount> m_submitted{};
        std::array<std::array<bool, kRegionCount>, kPlayerCount> m_flags{};
        std::array<KnightAllocation, kPlayerCount> m_knights{};
        std::vector<RegionEvent> m_resolution;
        std::vector<RoundRecord> m_history;
    };
}
