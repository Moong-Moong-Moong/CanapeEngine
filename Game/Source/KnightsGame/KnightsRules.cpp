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

    RegionEvent ResolveMonster(int region, int monsterScore, const std::array<int, kPlayerCount>& sent,
                               const std::array<bool, kPlayerCount>& flagged)
    {
        RegionEvent e;
        e.region = region;
        e.kind = RegionKind::Monster;
        e.monsterScore = monsterScore;
        e.sent = sent;
        e.flagged = flagged;
        for (int c : sent)
            e.totalKnights += c;
        if (e.totalKnights == 0)
            return e;

        // 점수보다 기사가 적으면 보낸 기사가 전멸한다.
        if (monsterScore > e.totalKnights)
        {
            e.wiped = true;
            e.knightsLost = sent;
            return e;
        }

        // 보낸 수가 적은 쪽부터 같은 수끼리 묶어 한 명당 그 수만큼 점수를 준다.
        // 남은 점수가 묶음 전체를 채우지 못하면 균등 분배하고 나머지는 폐기한다.
        int remaining = monsterScore;
        int c = 0;
        while (remaining > 0)
        {
            int next = 0;
            for (int s : sent)
            {
                if (s > c && (next == 0 || s < next))
                    next = s;
            }
            if (next == 0)
                break;
            c = next;
            int n = 0;
            for (int s : sent)
            {
                if (s == c)
                    ++n;
            }
            if (remaining >= c * n)
            {
                for (int p = 0; p < kPlayerCount; ++p)
                {
                    if (sent[p] == c)
                        e.scoreGained[p] = c;
                }
                remaining -= c * n;
            }
            else
            {
                const int share = remaining / n;
                for (int p = 0; p < kPlayerCount; ++p)
                {
                    if (sent[p] == c)
                        e.scoreGained[p] = share;
                }
                remaining -= share * n;
                break;
            }
        }
        e.discarded = remaining;
        return e;
    }

    SubmitError Game::SubmitKnights(int player, const KnightAllocation& allocation)
    {
        if (m_phase != Phase::KnightPlacement)
            return SubmitError::WrongPhase;
        if (player < 0 || player >= kPlayerCount)
            return SubmitError::InvalidPlayer;
        if (m_submitted[player])
            return SubmitError::AlreadySubmitted;

        const auto& defs = GetRegionDefs();
        int total = 0;
        for (int r = 0; r < kRegionCount; ++r)
        {
            if (allocation[r] < 0)
                return SubmitError::NegativeCount;
        }
        for (int r = 0; r < kRegionCount; ++r)
        {
            if (allocation[r] == 0)
                continue;
            if (!m_flags[player][r])
                return SubmitError::NoFlagThere;
            if (defs[r].kind != RegionKind::Monster)
                return SubmitError::NotMonsterRegion;
            total += allocation[r];
        }
        if (total > m_players[player].knights)
            return SubmitError::ExceedsKnights;

        m_knights[player] = allocation;
        m_submitted[player] = true;

        for (bool done : m_submitted)
        {
            if (!done)
                return SubmitError::None;
        }

        // 모두 제출하면 지역 0~5 순서로 정산한다.
        m_resolution.clear();
        for (int r = 0; r < kRegionCount; ++r)
        {
            std::array<bool, kPlayerCount> flagged{};
            std::array<int, kPlayerCount> sent{};
            for (int p = 0; p < kPlayerCount; ++p)
            {
                flagged[p] = m_flags[p][r];
                sent[p] = m_knights[p][r];
            }
            RegionEvent e;
            if (defs[r].kind == RegionKind::Monster)
            {
                e = ResolveMonster(r, m_monsterScores[r], sent, flagged);
            }
            else
            {
                // 증원 지역은 Task 3에서 채운다.
                e.region = r;
                e.kind = defs[r].kind;
                e.flagged = flagged;
            }
            for (int p = 0; p < kPlayerCount; ++p)
            {
                m_players[p].score += e.scoreGained[p];
                m_players[p].knights += e.knightsGained[p] - e.knightsLost[p];
            }
            m_resolution.push_back(e);
        }
        m_phase = Phase::Resolution;
        m_submitted = {};
        return SubmitError::None;
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
