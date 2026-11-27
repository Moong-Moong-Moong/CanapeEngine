// 기사단 게임 룰 코어 검증. 엔진에 링크하지 않고 룰 소스만 컴파일한다.
#include "KnightsGame/KnightsRules.h"

#include <cstdio>
#include <string_view>

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
        for (int p = 1; p < kPlayerCount; ++p)
            CHECK(g.GetPlayer(p).knights == kStartKnights && g.GetPlayer(p).score == 0);
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
    ZeroAllocationAccepted();
    KnightsHiddenUntilAll();
    WipeAcrossRegions();

    if (g_failures == 0)
    {
        std::printf("ALL PASSED (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("FAILED (%d)\n", g_failures);
    return 1;
}
