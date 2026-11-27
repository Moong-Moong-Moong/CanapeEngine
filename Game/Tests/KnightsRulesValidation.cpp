// 기사단 게임 룰 코어·AI·화면 문구 검증. 엔진에 링크하지 않고 엔진과 무관한 소스만 컴파일한다.
#include "KnightsGame/KnightsAi.h"
#include "KnightsGame/KnightsBoardText.h"
#include "KnightsGame/KnightsRules.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <utility>

using namespace Knights;

namespace
{
    int g_checks = 0;
    int g_failures = 0;
}

#define CHECK(expr)                                                              \
    do                                                                           \
    {                                                                            \
        ++g_checks;                                                              \
        if (!(expr))                                                             \
        {                                                                        \
            ++g_failures;                                                        \
            std::printf("%s(%d): CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
        }                                                                        \
    } while (false)

namespace
{
    // 지역 테이블이 Global Constraints와 정확히 같다.
    void RegionTable()
    {
        const auto& defs = GetRegionDefs();
        CHECK(defs.size() == 6);
        CHECK(defs[0].name == std::string_view("고블린 둥지"));
        CHECK(defs[0].kind == RegionKind::Monster && defs[0].minScore == 1 && defs[0].maxScore == 3);
        CHECK(defs[1].name == std::string_view("오크 야영지"));
        CHECK(defs[1].kind == RegionKind::Monster && defs[1].minScore == 3 && defs[1].maxScore == 6);
        CHECK(defs[2].name == std::string_view("트롤 다리"));
        CHECK(defs[2].kind == RegionKind::Monster && defs[2].minScore == 5 && defs[2].maxScore == 9);
        CHECK(defs[3].name == std::string_view("드래곤 동굴"));
        CHECK(defs[3].kind == RegionKind::Monster && defs[3].minScore == 8 && defs[3].maxScore == 12);
        CHECK(defs[4].name == std::string_view("훈련소"));
        CHECK(defs[4].kind == RegionKind::TrainingGround && defs[4].minScore == 0 && defs[4].maxScore == 0);
        CHECK(defs[5].name == std::string_view("용병 시장"));
        CHECK(defs[5].kind == RegionKind::MercenaryMarket && defs[5].minScore == 0 && defs[5].maxScore == 0);
    }

    void StartState()
    {
        Game g(1);
        CHECK(g.GetRound() == 1);
        CHECK(g.GetPhase() == Phase::FlagPlacement);
        for (int p = 0; p < kPlayerCount; ++p)
        {
            CHECK(g.GetPlayer(p).score == 0);
            CHECK(g.GetPlayer(p).knights == 5);
            CHECK(!g.HasSubmitted(p));
        }
        const auto& defs = GetRegionDefs();
        for (int r = 0; r < 4; ++r)
        {
            CHECK(g.GetMonsterScore(r) >= defs[r].minScore);
            CHECK(g.GetMonsterScore(r) <= defs[r].maxScore);
        }
        CHECK(g.GetMonsterScore(4) == 0);
        CHECK(g.GetMonsterScore(5) == 0);
    }

    void FlagValidation()
    {
        Game g(1);
        CHECK(g.SubmitFlags(0, FlagChoice{0, 0}) == SubmitError::DuplicateRegion);
        CHECK(g.SubmitFlags(0, FlagChoice{0, 6}) == SubmitError::InvalidRegion);
        CHECK(g.SubmitFlags(0, FlagChoice{-1, 2}) == SubmitError::InvalidRegion);
        CHECK(g.SubmitFlags(5, FlagChoice{0, 1}) == SubmitError::InvalidPlayer);
        CHECK(g.SubmitFlags(-1, FlagChoice{0, 1}) == SubmitError::InvalidPlayer);
        CHECK(!g.HasSubmitted(0));
        CHECK(g.SubmitFlags(0, FlagChoice{0, 4}) == SubmitError::None);
        CHECK(g.HasSubmitted(0));
    }

    void DoubleSubmitRejected()
    {
        Game g(1);
        CHECK(g.SubmitFlags(0, FlagChoice{0, 4}) == SubmitError::None);
        CHECK(g.SubmitFlags(0, FlagChoice{1, 2}) == SubmitError::AlreadySubmitted);
        const PublicView v = g.GetView(0);
        CHECK(v.flags[0][0] && v.flags[0][4]);
        CHECK(!v.flags[0][1] && !v.flags[0][2]);
    }

    void FlagsHiddenUntilAll()
    {
        Game g(1);
        CHECK(g.SubmitFlags(0, FlagChoice{0, 4}) == SubmitError::None);
        const PublicView other = g.GetView(1);
        for (int r = 0; r < kRegionCount; ++r)
            CHECK(!other.flags[0][r]);
        CHECK(other.submitted[0]);
        CHECK(g.GetView(0).flags[0][0]);

        for (int p = 1; p < kPlayerCount; ++p)
            CHECK(g.SubmitFlags(p, FlagChoice{1, 2}) == SubmitError::None);
        CHECK(g.GetPhase() == Phase::KnightPlacement);
        CHECK(g.GetView(1).flags[0][0]);
        CHECK(g.GetView(1).flags[0][4]);
        CHECK(g.GetView(1).phase == Phase::KnightPlacement);
        for (int p = 0; p < kPlayerCount; ++p)
            CHECK(!g.HasSubmitted(p));
    }

    void KnightsSubmitWrongPhase()
    {
        Game g(1);
        CHECK(g.SubmitKnights(0, KnightAllocation{}) == SubmitError::WrongPhase);
    }

    void FlagsSubmitWrongPhase()
    {
        Game g(1);
        for (int p = 0; p < kPlayerCount; ++p)
            CHECK(g.SubmitFlags(p, FlagChoice{0, 4}) == SubmitError::None);
        CHECK(g.GetPhase() == Phase::KnightPlacement);
        CHECK(g.SubmitFlags(0, FlagChoice{1, 2}) == SubmitError::WrongPhase);
        CHECK(g.GetPhase() == Phase::KnightPlacement);
        const PublicView v = g.GetView(0);
        CHECK(v.flags[0][0] && v.flags[0][4] && !v.flags[0][1] && !v.flags[0][2]);
        for (int p = 0; p < kPlayerCount; ++p)
            CHECK(!g.HasSubmitted(p));
    }

    using Arr = std::array<int, kPlayerCount>;
    using Flg = std::array<bool, kPlayerCount>;

    // 스펙 3.4 표. A=0, B=1, C=2.
    void MonsterTable()
    {
        const Flg all{true, true, true, true, true};
        {
            RegionEvent e = ResolveMonster(0, 1, Arr{1, 1, 0, 0, 0}, all);
            CHECK(e.scoreGained == (Arr{0, 0, 0, 0, 0}) && e.discarded == 1 && !e.wiped);
            CHECK(e.totalKnights == 2 && e.monsterScore == 1 && e.kind == RegionKind::Monster);
        }
        {
            RegionEvent e = ResolveMonster(1, 5, Arr{2, 5, 0, 0, 0}, all);
            CHECK(e.scoreGained == (Arr{2, 3, 0, 0, 0}) && e.discarded == 0 && !e.wiped);
        }
        {
            RegionEvent e = ResolveMonster(1, 4, Arr{1, 3, 0, 0, 0}, all);
            CHECK(e.scoreGained == (Arr{1, 3, 0, 0, 0}) && e.discarded == 0);
        }
        {
            RegionEvent e = ResolveMonster(1, 5, Arr{2, 2, 3, 0, 0}, all);
            CHECK(e.scoreGained == (Arr{2, 2, 1, 0, 0}) && e.discarded == 0);
        }
        {
            RegionEvent e = ResolveMonster(1, 5, Arr{3, 3, 0, 0, 0}, all);
            CHECK(e.scoreGained == (Arr{2, 2, 0, 0, 0}) && e.discarded == 1);
        }
        {
            RegionEvent e = ResolveMonster(1, 5, Arr{2, 4, 4, 0, 0}, all);
            CHECK(e.scoreGained == (Arr{2, 1, 1, 0, 0}) && e.discarded == 1);
        }
        {
            RegionEvent e = ResolveMonster(2, 7, Arr{2, 3, 0, 0, 0}, all);
            CHECK(e.wiped && e.knightsLost == (Arr{2, 3, 0, 0, 0}));
            CHECK(e.scoreGained == (Arr{0, 0, 0, 0, 0}) && e.discarded == 0);
        }
        {
            RegionEvent e = ResolveMonster(0, 3, Arr{3, 0, 0, 0, 0}, all);
            CHECK(e.scoreGained == (Arr{3, 0, 0, 0, 0}) && !e.wiped);
        }
        {
            RegionEvent e = ResolveMonster(0, 3, Arr{0, 0, 0, 0, 0}, Flg{true, false, false, false, false});
            CHECK(e.totalKnights == 0 && !e.wiped && e.discarded == 0);
            CHECK(e.scoreGained == (Arr{}) && e.knightsLost == (Arr{}) && e.flagged[0] && !e.flagged[1]);
        }
    }

    // 모두 같은 깃발로 KnightPlacement까지 진행한다.
    void ToKnightPlacement(Game& g, const FlagChoice& flags)
    {
        for (int p = 0; p < kPlayerCount; ++p)
            CHECK(g.SubmitFlags(p, flags) == SubmitError::None);
    }

    void KnightValidation()
    {
        Game g(1);
        ToKnightPlacement(g, FlagChoice{0, 4});
        CHECK(g.SubmitKnights(0, KnightAllocation{0, 1, 0, 0, 0, 0}) == SubmitError::NoFlagThere);
        CHECK(g.SubmitKnights(0, KnightAllocation{0, 0, 0, 0, 1, 0}) == SubmitError::NotMonsterRegion);
        CHECK(g.SubmitKnights(0, KnightAllocation{-1, 0, 0, 0, 0, 0}) == SubmitError::NegativeCount);
        CHECK(g.SubmitKnights(0, KnightAllocation{6, 0, 0, 0, 0, 0}) == SubmitError::ExceedsKnights);
        CHECK(g.SubmitKnights(9, KnightAllocation{}) == SubmitError::InvalidPlayer);
        CHECK(!g.HasSubmitted(0));
        CHECK(g.SubmitKnights(0, KnightAllocation{5, 0, 0, 0, 0, 0}) == SubmitError::None);
        CHECK(g.SubmitKnights(0, KnightAllocation{1, 0, 0, 0, 0, 0}) == SubmitError::AlreadySubmitted);
    }

    // 한 지역 값이 보유 기사보다 크면 합을 구하기 전에 거부한다. 큰 값 둘을 더하면 int가 넘쳐 음수 합이 될 수 있다.
    void HugeEntryRejected()
    {
        Game g(1);
        ToKnightPlacement(g, FlagChoice{0, 1});
        CHECK(g.SubmitKnights(0, KnightAllocation{INT_MAX, INT_MAX, 0, 0, 0, 0}) == SubmitError::ExceedsKnights);
        CHECK(g.SubmitKnights(0, KnightAllocation{INT_MAX, 1, 0, 0, 0, 0}) == SubmitError::ExceedsKnights);
        CHECK(g.SubmitKnights(0, KnightAllocation{6, 0, 0, 0, 0, 0}) == SubmitError::ExceedsKnights);
        // 깃발 없는 지역이라도 보유보다 큰 값은 ExceedsKnights로 먼저 거부된다.
        CHECK(g.SubmitKnights(0, KnightAllocation{0, 0, 0, INT_MAX, 0, 0}) == SubmitError::ExceedsKnights);
        CHECK(!g.HasSubmitted(0));
        CHECK(g.GetView(0).knights[0] == (KnightAllocation{}));
        CHECK(g.SubmitKnights(0, KnightAllocation{3, 2, 0, 0, 0, 0}) == SubmitError::None);
    }

    void ZeroAllocationAccepted()
    {
        Game g(1);
        ToKnightPlacement(g, FlagChoice{4, 5});
        CHECK(g.SubmitKnights(0, KnightAllocation{}) == SubmitError::None);
        CHECK(g.HasSubmitted(0));
    }

    void KnightsHiddenUntilAll()
    {
        Game g(1);
        ToKnightPlacement(g, FlagChoice{0, 4});
        CHECK(g.SubmitKnights(0, KnightAllocation{3, 0, 0, 0, 0, 0}) == SubmitError::None);
        const PublicView other = g.GetView(1);
        for (int r = 0; r < kRegionCount; ++r)
            CHECK(other.knights[0][r] == 0);
        CHECK(other.submitted[0]);
        CHECK(g.GetView(0).knights[0][0] == 3);
        CHECK(g.GetPhase() == Phase::KnightPlacement);
        for (int p = 1; p < kPlayerCount; ++p)
            CHECK(g.SubmitKnights(p, KnightAllocation{1, 0, 0, 0, 0, 0}) == SubmitError::None);
        CHECK(g.GetPhase() == Phase::Resolution);
        CHECK(g.GetView(1).knights[0][0] == 3);
        CHECK(g.GetView(1).knights[2][0] == 1);
        CHECK(g.GetResolution().size() == kRegionCount);
    }

    void WipeAcrossRegions()
    {
        Game g(7);
        // 다른 플레이어는 증원 지역에만 깃발. 플레이어 0은 고블린(0)과 드래곤(3)에 1명, 4명.
        CHECK(g.SubmitFlags(0, FlagChoice{0, 3}) == SubmitError::None);
        for (int p = 1; p < kPlayerCount; ++p)
            CHECK(g.SubmitFlags(p, FlagChoice{4, 5}) == SubmitError::None);
        CHECK(g.SubmitKnights(0, KnightAllocation{1, 0, 0, 4, 0, 0}) == SubmitError::None);
        for (int p = 1; p < kPlayerCount; ++p)
            CHECK(g.SubmitKnights(p, KnightAllocation{}) == SubmitError::None);
        CHECK(g.GetPhase() == Phase::Resolution);

        int score = 0, lost = 0, gained = 0;
        bool dragonWiped = false;
        const auto& events = g.GetResolution();
        CHECK(events.size() == kRegionCount);
        for (int i = 0; i < static_cast<int>(events.size()); ++i)
        {
            const RegionEvent& e = events[i];
            CHECK(e.region == i);
            score += e.scoreGained[0];
            lost += e.knightsLost[0];
            gained += e.knightsGained[0];
            if (i == 3 && e.wiped)
                dragonWiped = true;
        }
        CHECK(dragonWiped);  // 드래곤 점수 8 이상, 보낸 기사 4명이므로 항상 전멸
        CHECK(lost >= 4);
        CHECK(g.GetPlayer(0).score == score);
        CHECK(g.GetPlayer(0).knights == kStartKnights - lost + gained);
        CHECK(g.GetPlayer(0).knights >= 0);
        // 나머지는 훈련소(+1)와 용병 시장(주사위 4명 분배)만 받는다.
        const int marketShare = events[5].dice / 4;
        for (int p = 1; p < kPlayerCount; ++p)
            CHECK(g.GetPlayer(p).knights == kStartKnights + 1 + marketShare && g.GetPlayer(p).score == 0);
    }

    void TrainingGround()
    {
        RegionEvent e = ResolveTraining(4, Flg{true, false, true, false, false});
        CHECK(e.region == 4 && e.kind == RegionKind::TrainingGround);
        CHECK(e.knightsGained == (Arr{1, 0, 1, 0, 0}));
        CHECK(e.flagged == (Flg{true, false, true, false, false}));
        CHECK(e.scoreGained == (Arr{}) && e.knightsLost == (Arr{}));
    }

    void Market()
    {
        {
            RegionEvent e = ResolveMarket(5, 1, Flg{true, true, false, false, false});
            CHECK(e.kind == RegionKind::MercenaryMarket && e.region == 5);
            CHECK(e.knightsGained == (Arr{0, 0, 0, 0, 0}) && e.discarded == 1 && e.dice == 1);
        }
        {
            RegionEvent e = ResolveMarket(5, 6, Flg{true, true, true, true, false});
            CHECK(e.knightsGained == (Arr{1, 1, 1, 1, 0}) && e.discarded == 2 && e.dice == 6);
        }
        {
            RegionEvent e = ResolveMarket(5, 5, Flg{false, false, false, true, false});
            CHECK(e.knightsGained == (Arr{0, 0, 0, 5, 0}) && e.discarded == 0);
        }
        {
            RegionEvent e = ResolveMarket(5, 4, Flg{});
            CHECK(e.dice == 0 && e.discarded == 0 && e.knightsGained == (Arr{}));
        }
    }

    // 정산 직후 증원이 knights에 반영되고, 다음 라운드 배치에서 쓸 수 있다.
    void ReinforceNextRound()
    {
        Game g(3);
        CHECK(g.SubmitFlags(0, FlagChoice{4, 0}) == SubmitError::None);
        for (int p = 1; p < kPlayerCount; ++p)
            CHECK(g.SubmitFlags(p, FlagChoice{1, 2}) == SubmitError::None);
        for (int p = 0; p < kPlayerCount; ++p)
            CHECK(g.SubmitKnights(p, KnightAllocation{}) == SubmitError::None);
        CHECK(g.GetPhase() == Phase::Resolution);
        CHECK(g.GetPlayer(0).knights == kStartKnights + 1);
        CHECK(g.GetResolution()[4].knightsGained[0] == 1);
        CHECK(g.GetResolution()[5].dice == 0);
        for (int p = 1; p < kPlayerCount; ++p)
            CHECK(g.GetPlayer(p).knights == kStartKnights);

        g.AdvanceRound();
        CHECK(g.GetRound() == 2 && g.GetPhase() == Phase::FlagPlacement);
        ToKnightPlacement(g, FlagChoice{0, 4});
        CHECK(g.SubmitKnights(0, KnightAllocation{7, 0, 0, 0, 0, 0}) == SubmitError::ExceedsKnights);
        CHECK(g.SubmitKnights(0, KnightAllocation{6, 0, 0, 0, 0, 0}) == SubmitError::None);
    }

    void MarketDiceInGame()
    {
        Game g(11);
        CHECK(g.SubmitFlags(0, FlagChoice{5, 0}) == SubmitError::None);
        for (int p = 1; p < kPlayerCount; ++p)
            CHECK(g.SubmitFlags(p, FlagChoice{1, 2}) == SubmitError::None);
        for (int p = 0; p < kPlayerCount; ++p)
            CHECK(g.SubmitKnights(p, KnightAllocation{}) == SubmitError::None);
        const RegionEvent& m = g.GetResolution()[5];
        CHECK(m.dice >= 1 && m.dice <= 6);
        CHECK(m.knightsGained[0] == m.dice && m.discarded == 0);
        CHECK(g.GetPlayer(0).knights == kStartKnights + m.dice);
    }

    void PlayRound(Game& g)
    {
        for (int p = 0; p < kPlayerCount; ++p)
            CHECK(g.SubmitFlags(p, FlagChoice{4, 5}) == SubmitError::None);
        for (int p = 0; p < kPlayerCount; ++p)
            CHECK(g.SubmitKnights(p, KnightAllocation{}) == SubmitError::None);
    }

    void RoundsAndGameOver()
    {
        Game g(5);
        CHECK(g.GetWinners().empty());  // GameOver 전에는 비어 있다
        for (int round = 1; round <= kRoundCount; ++round)
        {
            CHECK(g.GetRound() == round);
            CHECK(g.GetPhase() == Phase::FlagPlacement);
            for (int p = 0; p < kPlayerCount; ++p)
                CHECK(!g.HasSubmitted(p));
            PlayRound(g);
            CHECK(g.GetPhase() == Phase::Resolution);
            g.AdvanceRound();
        }
        CHECK(g.GetPhase() == Phase::GameOver);
        CHECK(g.GetRound() == kRoundCount);
        CHECK(g.SubmitFlags(0, FlagChoice{4, 5}) == SubmitError::WrongPhase);
        CHECK(g.SubmitKnights(0, KnightAllocation{}) == SubmitError::WrongPhase);
        g.AdvanceRound();  // GameOver에서는 무시
        CHECK(g.GetPhase() == Phase::GameOver && g.GetRound() == kRoundCount);
        CHECK(!g.GetWinners().empty());
    }

    void AdvanceOnlyInResolution()
    {
        Game g(1);
        g.AdvanceRound();
        CHECK(g.GetRound() == 1 && g.GetPhase() == Phase::FlagPlacement);
        ToKnightPlacement(g, FlagChoice{4, 5});
        g.AdvanceRound();
        CHECK(g.GetRound() == 1 && g.GetPhase() == Phase::KnightPlacement);
    }

    // 정산 기록은 Resolution 단계에서만 있다. 다음 라운드와 GameOver에서는 비어 있다.
    void ResolutionClearedAfterAdvance()
    {
        Game g(9);
        for (int round = 1; round <= kRoundCount; ++round)
        {
            CHECK(g.GetResolution().empty());
            PlayRound(g);
            CHECK(g.GetPhase() == Phase::Resolution);
            CHECK(g.GetResolution().size() == kRegionCount);
            g.AdvanceRound();
            CHECK(g.GetResolution().empty());
        }
        CHECK(g.GetPhase() == Phase::GameOver);
        CHECK(g.GetResolution().empty());
    }

    void Winners()
    {
        using Players = std::array<PlayerState, kPlayerCount>;
        auto make = [](const Arr& scores, const Arr& knights)
        {
            Players ps{};
            for (int p = 0; p < kPlayerCount; ++p)
            {
                ps[p].score = scores[p];
                ps[p].knights = knights[p];
            }
            return ps;
        };
        // 단독 1위
        CHECK(RankWinners(make(Arr{3, 5, 1, 1, 0}, Arr{9, 2, 4, 9, 9})) == (std::vector<int>{1}));
        // 점수 동점이면 기사가 많은 쪽
        CHECK(RankWinners(make(Arr{3, 5, 5, 1, 0}, Arr{9, 2, 4, 9, 9})) == (std::vector<int>{2}));
        // 점수·기사 모두 같으면 공동 우승
        CHECK(RankWinners(make(Arr{5, 5, 1, 0, 0}, Arr{3, 3, 9, 9, 9})) == (std::vector<int>{0, 1}));
    }

    void ResetRestoresStart()
    {
        Game g(5);
        for (int i = 0; i < 3; ++i)
        {
            PlayRound(g);
            g.AdvanceRound();
        }
        CHECK(g.GetRound() == 4);
        CHECK(g.SubmitFlags(0, FlagChoice{4, 5}) == SubmitError::None);
        g.Reset(7);
        CHECK(g.GetRound() == 1 && g.GetPhase() == Phase::FlagPlacement);
        CHECK(g.GetResolution().empty());
        for (int p = 0; p < kPlayerCount; ++p)
        {
            CHECK(g.GetPlayer(p).score == 0 && g.GetPlayer(p).knights == kStartKnights);
            CHECK(!g.HasSubmitted(p));
        }
        const PublicView v = g.GetView(0);
        for (int p = 0; p < kPlayerCount; ++p)
        {
            for (int r = 0; r < kRegionCount; ++r)
                CHECK(!v.flags[p][r] && v.knights[p][r] == 0);
        }
        Game fresh(7);
        for (int r = 0; r < 4; ++r)
            CHECK(g.GetMonsterScore(r) == fresh.GetMonsterScore(r));
    }

    constexpr std::array<AiPersonality, 4> kAllPersonalities = {
        AiPersonality::Aggressive, AiPersonality::Cautious, AiPersonality::Reinforcer, AiPersonality::Balanced};

    void AiPersonalityMapping()
    {
        CHECK(GetAiPersonality(1) == AiPersonality::Aggressive);
        CHECK(GetAiPersonality(2) == AiPersonality::Cautious);
        CHECK(GetAiPersonality(3) == AiPersonality::Reinforcer);
        CHECK(GetAiPersonality(4) == AiPersonality::Balanced);
    }

    // 시드 1..1000 자가 대전. 플레이어 0도 균형형 AI로 대체한다. 모든 제출이 None이고 불변식이 지켜진다.
    void AiAlwaysValid()
    {
        const auto& defs = GetRegionDefs();
        constexpr uint32_t kGames = 1000;
        // AI 1~4(성향별)가 모든 게임에서 보낸 기사 합. "항상 0명만 보내는" 퇴화를 잡는다.
        std::array<long long, kPlayerCount> sentTotal{};
        for (uint32_t seed = 1; seed <= kGames; ++seed)
        {
            Game g(seed);
            std::vector<Ai> ais;
            ais.emplace_back(AiPersonality::Balanced, seed * kPlayerCount);
            for (int p = 1; p < kPlayerCount; ++p)
                ais.emplace_back(GetAiPersonality(p), seed * kPlayerCount + static_cast<uint32_t>(p));

            for (int round = 1; round <= kRoundCount; ++round)
            {
                CHECK(g.GetRound() == round && g.GetPhase() == Phase::FlagPlacement);
                for (int r = 0; r < kRegionCount; ++r)
                {
                    if (defs[r].kind == RegionKind::Monster)
                        CHECK(g.GetMonsterScore(r) >= defs[r].minScore && g.GetMonsterScore(r) <= defs[r].maxScore);
                }
                for (int p = 0; p < kPlayerCount; ++p)
                    CHECK(g.SubmitFlags(p, ais[p].ChooseFlags(g.GetView(p))) == SubmitError::None);
                CHECK(g.GetPhase() == Phase::KnightPlacement);
                for (int p = 0; p < kPlayerCount; ++p)
                {
                    const KnightAllocation a = ais[p].ChooseKnights(g.GetView(p));
                    for (int count : a)
                        sentTotal[p] += count;
                    CHECK(g.SubmitKnights(p, a) == SubmitError::None);
                }
                CHECK(g.GetPhase() == Phase::Resolution);
                for (const RegionEvent& e : g.GetResolution())
                    CHECK(e.dice >= 0 && e.dice <= 6);
                for (int p = 0; p < kPlayerCount; ++p)
                    CHECK(g.GetPlayer(p).knights >= 0);
                g.AdvanceRound();
            }
            CHECK(g.GetPhase() == Phase::GameOver);
            CHECK(!g.GetWinners().empty());
        }

        // AI(1~4)가 실제로 기사를 보내고, 성향마다 라운드당 평균 보낸 수가 모두 같지는 않다.
        long long aiTotal = 0;
        std::set<long long> distinct;
        const double rounds = static_cast<double>(kGames) * kRoundCount;
        for (int p = 1; p < kPlayerCount; ++p)
        {
            aiTotal += sentTotal[p];
            distinct.insert(sentTotal[p]);
            std::printf("  %s (%s) average knights sent per round: %.3f\n", PlayerName(p).c_str(), PersonalityName(p).c_str(),
                        static_cast<double>(sentTotal[p]) / rounds);
        }
        CHECK(aiTotal > 0);
        CHECK(distinct.size() >= 2);
    }

    // 나 혼자 깃발을 꽂은 지역에서 보유 기사가 몬스터 점수보다 적으면 한 명도 보내지 않는다.
    void AiNoHopelessSolo()
    {
        PublicView v;
        v.viewer = 2;
        v.round = 1;
        v.phase = Phase::KnightPlacement;
        v.monsterScores = {2, 4, 7, 12, 0, 0};
        v.flags[2][3] = true;  // 드래곤 동굴(점수 12): 나만 깃발
        v.flags[2][4] = true;  // 훈련소
        v.players[2].knights = 5;
        for (AiPersonality personality : kAllPersonalities)
        {
            for (uint32_t seed = 1; seed <= 20; ++seed)
            {
                Ai ai(personality, seed);
                const KnightAllocation a = ai.ChooseKnights(v);
                CHECK(a[3] == 0);
                for (int r = 0; r < kRegionCount; ++r)
                    CHECK(a[r] == 0);
            }
        }
    }

    // 같은 뷰에서도 시드에 따라 깃발 선택이 달라진다.
    void AiVaries()
    {
        const Game g(1);
        const PublicView v = g.GetView(4);
        std::set<std::pair<int, int>> seen;
        for (uint32_t seed = 1; seed <= 50; ++seed)
        {
            Ai ai(AiPersonality::Balanced, seed);
            const FlagChoice f = ai.ChooseFlags(v);
            CHECK(f[0] != f[1] && f[0] >= 0 && f[1] >= 0 && f[0] < kRegionCount && f[1] < kRegionCount);
            seen.insert({std::min(f[0], f[1]), std::max(f[0], f[1])});
        }
        CHECK(seen.size() >= 2);
    }

    bool Has(const std::string& text, std::string_view part)
    {
        return text.find(part) != std::string::npos;
    }

    // 화면 문구: 전체 문장이 아니라 핵심 부분 문자열만 확인한다(문장 다듬기에 테스트가 깨지지 않게).
    void EventTextWin()
    {
        const Flg flags{true, true, false, false, false};
        const EventText t = DescribeEvent(ResolveMonster(1, 5, Arr{2, 3, 0, 0, 0}, flags));
        CHECK(t.tone == EventTone::Win);
        CHECK(Has(t.headline, "오크 야영지") && Has(t.headline, "승리"));
        CHECK(Has(t.headline, "나 +2") && Has(t.headline, "AI 1 +3"));
        CHECK(!Has(t.headline, "폐기"));
        CHECK(Has(t.cardLine, "승리") && Has(t.cardLine, "나 +2") && Has(t.cardLine, "AI 1 +3"));
        CHECK(Has(t.detail, "5점"));
    }

    void EventTextWipe()
    {
        const Flg flags{true, true, false, false, false};
        const EventText t = DescribeEvent(ResolveMonster(2, 7, Arr{2, 3, 0, 0, 0}, flags));
        CHECK(t.tone == EventTone::Wipe);
        CHECK(Has(t.headline, "트롤 다리") && Has(t.headline, "전멸"));
        CHECK(Has(t.headline, "나 기사 2명") && Has(t.headline, "AI 1 기사 3명"));
        CHECK(Has(t.cardLine, "전멸") && Has(t.cardLine, "나 -2") && Has(t.cardLine, "AI 1 -3"));
        CHECK(!Has(t.headline, "+"));
    }

    void EventTextDiscard()
    {
        const Flg flags{true, true, false, false, false};
        const EventText t = DescribeEvent(ResolveMonster(1, 5, Arr{3, 3, 0, 0, 0}, flags));
        CHECK(t.tone == EventTone::Win);
        CHECK(Has(t.headline, "나 +2") && Has(t.headline, "AI 1 +2"));
        CHECK(Has(t.headline, "1점 폐기"));
        CHECK(Has(t.cardLine, "1점 폐기"));
    }

    void EventTextMarketZero()
    {
        // 주사위 1을 2명이 나누면 0명씩, 1명 폐기.
        const EventText t = DescribeEvent(ResolveMarket(5, 1, Flg{true, false, true, false, false}));
        CHECK(t.tone == EventTone::Reinforce);
        CHECK(Has(t.headline, "용병 시장") && Has(t.headline, "주사위 1"));
        CHECK(Has(t.headline, "2명이 0명씩") && Has(t.headline, "1명 폐기"));
        CHECK(Has(t.detail, "나") && Has(t.detail, "AI 2"));
        CHECK(Has(t.cardLine, "0명씩"));
    }

    void EventTextFlagsNoKnights()
    {
        const EventText t = DescribeEvent(ResolveMonster(0, 3, Arr{}, Flg{true, false, false, true, false}));
        CHECK(t.tone == EventTone::Nothing);
        CHECK(Has(t.headline, "고블린 둥지") && Has(t.headline, "아무도 싸우지 않음"));
        CHECK(Has(t.cardLine, "싸운 기사 없음"));
        CHECK(!Has(t.headline, "승리") && !Has(t.headline, "전멸"));

        // 깃발 자체가 없을 때는 따로 알린다.
        const EventText none = DescribeEvent(ResolveMonster(0, 3, Arr{}, Flg{}));
        CHECK(Has(none.headline, "깃발 없음") && none.tone == EventTone::Nothing);
    }
}

int main()
{
    RegionTable();
    StartState();
    FlagValidation();
    DoubleSubmitRejected();
    FlagsHiddenUntilAll();
    KnightsSubmitWrongPhase();
    FlagsSubmitWrongPhase();
    MonsterTable();
    KnightValidation();
    HugeEntryRejected();
    ZeroAllocationAccepted();
    KnightsHiddenUntilAll();
    WipeAcrossRegions();
    TrainingGround();
    Market();
    ReinforceNextRound();
    MarketDiceInGame();
    RoundsAndGameOver();
    AdvanceOnlyInResolution();
    ResolutionClearedAfterAdvance();
    Winners();
    ResetRestoresStart();
    AiPersonalityMapping();
    AiAlwaysValid();
    AiNoHopelessSolo();
    AiVaries();
    EventTextWin();
    EventTextWipe();
    EventTextDiscard();
    EventTextMarketZero();
    EventTextFlagsNoKnights();

    if (g_failures == 0)
    {
        std::printf("ALL PASSED (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("FAILED (%d)\n", g_failures);
    return 1;
}
