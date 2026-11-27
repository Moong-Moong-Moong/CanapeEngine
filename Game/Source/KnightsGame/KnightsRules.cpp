#include "KnightsGame/KnightsRules.h"

namespace Knights
{
    const std::array<RegionDef, kRegionCount>& GetRegionDefs()
    {
        static const std::array<RegionDef, kRegionCount> kDefs = {{
            {"고블린 둥지", RegionKind::Monster, 1, 3},
            {"오크 야영지", RegionKind::Monster, 3, 6},
            {"트롤 다리", RegionKind::Monster, 5, 9},
            {"드래곤 동굴", RegionKind::Monster, 8, 12},
            {"훈련소", RegionKind::TrainingGround, 0, 0},
            {"용병 시장", RegionKind::MercenaryMarket, 0, 0},
        }};
        return kDefs;
    }

    Game::Game(uint32_t seed)
    {
        Reset(seed);
    }

    void Game::Reset(uint32_t seed)
    {
        m_rng.seed(seed);
        m_round = 1;
        m_phase = Phase::FlagPlacement;
        m_players = {};
        m_submitted = {};
        m_flags = {};
        m_knights = {};
        m_resolution.clear();
        RollMonsterScores();
    }

    void Game::RollMonsterScores()
    {
        const auto& defs = GetRegionDefs();
        for (int r = 0; r < kRegionCount; ++r)
        {
            if (defs[r].kind == RegionKind::Monster)
            {
                std::uniform_int_distribution<int> dist(defs[r].minScore, defs[r].maxScore);
                m_monsterScores[r] = dist(m_rng);
            }
            else
            {
                m_monsterScores[r] = 0;
            }
        }
    }

    SubmitError Game::SubmitFlags(int player, const FlagChoice& regions)
    {
        if (m_phase != Phase::FlagPlacement)
            return SubmitError::WrongPhase;
        if (player < 0 || player >= kPlayerCount)
            return SubmitError::InvalidPlayer;
        if (m_submitted[player])
            return SubmitError::AlreadySubmitted;
        for (int region : regions)
        {
            if (region < 0 || region >= kRegionCount)
                return SubmitError::InvalidRegion;
        }
        if (regions[0] == regions[1])
            return SubmitError::DuplicateRegion;

        m_flags[player] = {};
        for (int region : regions)
            m_flags[player][region] = true;
        m_submitted[player] = true;

        // 모두 제출하면 깃발이 공개되고 기사 배치 단계로 넘어간다.
        for (bool done : m_submitted)
        {
            if (!done)
                return SubmitError::None;
        }
        m_phase = Phase::KnightPlacement;
        m_submitted = {};
        return SubmitError::None;
    }

    SubmitError Game::SubmitKnights(int, const KnightAllocation&)
    {
        // Task 2에서 구현한다.
        return SubmitError::WrongPhase;
    }

    void Game::AdvanceRound()
    {
        // Task 3에서 구현한다.
    }

    std::vector<int> Game::GetWinners() const
    {
        // Task 3에서 구현한다.
        return {};
    }

    PublicView Game::GetView(int viewer) const
    {
        PublicView view;
        view.viewer = viewer;
        view.round = m_round;
        view.phase = m_phase;
        view.players = m_players;
        view.monsterScores = m_monsterScores;
        view.submitted = m_submitted;

        const bool flagsRevealed = m_phase != Phase::FlagPlacement;
        const bool knightsRevealed = m_phase == Phase::Resolution || m_phase == Phase::GameOver;
        for (int p = 0; p < kPlayerCount; ++p)
        {
            if (flagsRevealed || p == viewer)
                view.flags[p] = m_flags[p];
            if (knightsRevealed || p == viewer)
                view.knights[p] = m_knights[p];
        }
        return view;
    }
}
