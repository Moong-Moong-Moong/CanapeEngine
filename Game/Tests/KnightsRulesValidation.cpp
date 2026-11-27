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
}

int main()
{
    RegionTable();
    StartState();
    FlagValidation();
    DoubleSubmitRejected();
    FlagsHiddenUntilAll();
    KnightsSubmitWrongPhase();

    if (g_failures == 0)
    {
        std::printf("ALL PASSED (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("FAILED (%d)\n", g_failures);
    return 1;
}
