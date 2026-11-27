#include "KnightsGame/KnightsAi.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Knights
{
    namespace
    {
        constexpr int kSamples = 12;                   // 남의 배치를 추정할 무작위 표본 수
        constexpr double kOtherCommitRate = 0.75;      // 남은 보유 기사의 이 비율을 몬스터 깃발 수로 나눠 보낸다고 추정
        constexpr double kKnightValuePerRound = 0.45;  // 기사 1명이 남은 라운드 하나에서 벌어 올 점수 추정
        constexpr double kKnightSaturation = 12.0;     // 기사가 많을수록 1명의 가치가 줄어드는 정도
        constexpr double kTiebreakKnightValue = 0.1;   // 마지막 라운드에도 동점 처리용으로 남는 기사 가치
        constexpr int kLowKnights = 4;                 // 증원형이 증원 지역을 우선하는 보유 기사 기준
        constexpr double kPickCutoff = 4.0;            // 최선보다 온도 × 이 값 이상 나쁜 후보는 고르지 않는다

        // 깃발 단계에서 남의 깃발 위치는 모르므로 보유 기사만으로 확률을 둔다.
        constexpr double kFlagMonsterProb = 0.3;       // 몬스터 지역 하나에 깃발을 꽂을 확률
        constexpr double kFlagMonsterProbLow = 0.2;    // 보유 기사가 kLowKnights 이하일 때
        constexpr double kFlagReinforceProb = 0.3;     // 증원 지역 하나에 깃발을 꽂을 확률
        constexpr double kFlagReinforceProbLow = 0.5;  // 보유 기사가 kLowKnights 이하일 때
        constexpr double kBothMonsterProb = 0.5;       // 몬스터에 꽂았다면 나머지 깃발도 몬스터일 확률

        // 성향별 가중치. 같은 평가식을 쓰고 가중치만 다르다.
        struct Traits
        {
            double lossWeight;               // 전멸로 잃는 기사 가치에 곱하는 배수
            double commitBonus;              // 보낸 기사 1명당 보너스(몬스터 점수 비례) — 공격형
            double overSendPenalty;          // 남의 추정 평균보다 많이 보낸 기사 1명당 벌점 — 신중형
            double bigMonsterBonus;          // 깃발 단계에서 몬스터 점수 1당 보너스 — 공격형
            double reinforceScale;           // 증원 가치 배수
            double lowKnightReinforceScale;  // 보유 기사 kLowKnights 이하일 때 증원 가치 배수
            double lowKnightReinforceBonus;  // 그때 증원 지역마다 더하는 점수
            double flagTemperature;          // 깃발 가중 무작위 온도(클수록 예측하기 어렵다)
            double knightTemperature;        // 기사 배치 가중 무작위 온도
        };

        const Traits& GetTraits(AiPersonality personality)
        {
            // 공격형: 손실을 덜 두려워하고 많이·크게 보낸다. 가장 변덕스럽다.
            static const Traits kAggressive{0.5, 0.3, 0.0, 0.2, 0.6, 1.0, 0.0, 1.0, 0.5};
            // 신중형: 전멸 손실을 크게 보고, 남보다 적게 보내 "작은 묶음 먼저" 이득을 노린다.
            static const Traits kCautious{1.4, 0.0, 0.3, 0.0, 1.0, 1.2, 0.0, 0.6, 0.35};
            // 증원형: 기사가 적으면 증원 지역을 강하게 우선하고, 기사를 아낀다.
            static const Traits kReinforcer{1.2, 0.0, 0.0, 0.0, 1.5, 2.2, 1.5, 0.7, 0.4};
            // 균형형: 보정 없이 기대값.
            static const Traits kBalanced{1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.7, 0.4};
            switch (personality)
            {
            case AiPersonality::Aggressive: return kAggressive;
            case AiPersonality::Cautious: return kCautious;
            case AiPersonality::Reinforcer: return kReinforcer;
            default: return kBalanced;
            }
        }

        int RemainingRounds(int round)
        {
            return std::max(0, kRoundCount - round);
        }

        // 기사 1명의 가치(점수 단위). 남은 라운드가 많고 보유 기사가 적을수록 크다.
        double KnightValue(int knights, int round)
        {
            const double v = kKnightValuePerRound * RemainingRounds(round) * kKnightSaturation /
                             (kKnightSaturation + std::max(0, knights));
            return std::max(v, kTiebreakKnightValue);
        }

        // 몬스터 지역 하나에 c명(0..maxSend)을 보냈을 때의 기대 이득.
        struct Outlook
        {
            int maxSend = 0;
            std::array<double, kMaxMonsterScore + 1> utility{};
        };

        // 표본 하나 = 다른 플레이어들이 그 지역에 보낼 기사 수 추정(viewer 자리는 0).
        using Samples = std::array<std::array<int, kPlayerCount>, kSamples>;

        // 추정 모델 공통 입력. 남의 정보는 공개 보유 기사·공개 깃발 수뿐이다.
        struct OthersEstimate
        {
            Samples samples{};
            int reachable = 0;          // 그 지역에 올 수 있는 남의 기사 상한(보유 합)
            double averageSend = 0.0;   // 보낼 만한 남 한 명당 평균 추정
            bool anyone = false;        // 남이 올 가능성이 있는가
        };

        int SampleBinomial(int trials, double probability, std::mt19937& rng)
        {
            if (trials <= 0 || probability <= 0.0)
                return 0;
            std::binomial_distribution<int> dist(trials, std::min(1.0, probability));
            return dist(rng);
        }

        // 기사 배치 단계: 깃발이 공개되었으므로 실제로 그 지역에 깃발을 꽂은 남만 센다.
        OthersEstimate EstimateRevealed(const PublicView& view, int region, std::mt19937& rng)
        {
            const auto& defs = GetRegionDefs();
            OthersEstimate est;
            std::array<double, kPlayerCount> rate{};
            int senders = 0;
            for (int p = 0; p < kPlayerCount; ++p)
            {
                const int knights = view.players[p].knights;
                if (p == view.viewer || !view.flags[p][region] || knights <= 0)
                    continue;
                int monsterFlags = 0;
                for (int r = 0; r < kRegionCount; ++r)
                {
                    if (view.flags[p][r] && defs[r].kind == RegionKind::Monster)
                        ++monsterFlags;
                }
                rate[p] = kOtherCommitRate / std::max(1, monsterFlags);
                est.reachable += knights;
                est.averageSend += knights * rate[p];
                ++senders;
            }
            est.anyone = senders > 0;
            if (senders > 0)
                est.averageSend /= senders;
            for (auto& sample : est.samples)
            {
                for (int p = 0; p < kPlayerCount; ++p)
                    sample[p] = SampleBinomial(view.players[p].knights, rate[p], rng);
            }
            return est;
        }

        // 깃발 단계: 남의 깃발은 아직 모르므로 보유 기사로 깃발 확률을 추정해 표본을 뽑는다.
        OthersEstimate EstimateHidden(const PublicView& view, std::mt19937& rng)
        {
            OthersEstimate est;
            std::uniform_real_distribution<double> unit(0.0, 1.0);
            const double expectedFlags = 1.0 + kBothMonsterProb;
            int senders = 0;
            for (int p = 0; p < kPlayerCount; ++p)
            {
                const int knights = view.players[p].knights;
                if (p == view.viewer || knights <= 0)
                    continue;
                est.reachable += knights;
                est.averageSend += knights * kOtherCommitRate / expectedFlags;
                ++senders;
            }
            est.anyone = senders > 0;
            if (senders > 0)
                est.averageSend /= senders;
            for (auto& sample : est.samples)
            {
                for (int p = 0; p < kPlayerCount; ++p)
                {
                    const int knights = view.players[p].knights;
                    sample[p] = 0;
                    if (p == view.viewer || knights <= 0)
                        continue;
                    const double flagProb = knights <= kLowKnights ? kFlagMonsterProbLow : kFlagMonsterProb;
                    if (unit(rng) >= flagProb)
                        continue;
                    const int monsterFlags = unit(rng) < kBothMonsterProb ? 2 : 1;
                    sample[p] = SampleBinomial(knights, kOtherCommitRate / monsterFlags, rng);
                }
            }
            return est;
        }

        // 표본마다 실제 정산 규칙(ResolveMonster)으로 내 득점과 손실을 계산해 평균 낸다.
        Outlook EvaluateMonster(const PublicView& view, int region, const OthersEstimate& others, const Traits& traits)
        {
            Outlook out;
            const int viewer = view.viewer;
            const int score = view.monsterScores[region];
            const int myKnights = std::max(0, view.players[viewer].knights);
            // 남이 전부 와도 점수에 못 미치면 확정 전멸이므로 보내지 않는다(혼자인데 기사 부족 포함).
            if (score <= 0 || myKnights + others.reachable < score)
                return out;
            // 점수보다 많이 보내면 득점은 늘지 않고 분배 순서만 늦어진다.
            out.maxSend = std::min({myKnights, score, kMaxMonsterScore});

            const double lossCost = traits.lossWeight * KnightValue(myKnights, view.round);
            std::array<bool, kPlayerCount> flagged{};
            flagged[viewer] = true;
            for (int c = 1; c <= out.maxSend; ++c)
            {
                double total = 0.0;
                for (const auto& sample : others.samples)
                {
                    std::array<int, kPlayerCount> sent = sample;
                    sent[viewer] = c;
                    const RegionEvent e = ResolveMonster(region, score, sent, flagged);
                    total += e.scoreGained[viewer] - lossCost * e.knightsLost[viewer];
                }
                double u = total / kSamples;
                u += traits.commitBonus * c * score / kMaxMonsterScore;
                if (others.anyone)
                    u -= traits.overSendPenalty * std::max(0.0, c - others.averageSend);
                out.utility[c] = u;
            }
            return out;
        }

        // 용병 시장 깃발 하나의 기대 기사 수 floor(d / n). d는 공개된 주사위, 남이 꽂을 확률은 보유 기사로만 추정한다.
        double ExpectedMarketKnights(const PublicView& view)
        {
            // 남의 인원 분포를 동적 계획법으로 구한다.
            std::array<double, kPlayerCount> dist{};
            dist[0] = 1.0;
            for (int p = 0; p < kPlayerCount; ++p)
            {
                if (p == view.viewer)
                    continue;
                const double q = view.players[p].knights <= kLowKnights ? kFlagReinforceProbLow : kFlagReinforceProb;
                for (int n = kPlayerCount - 1; n >= 1; --n)
                    dist[n] = dist[n] * (1.0 - q) + dist[n - 1] * q;
                dist[0] *= 1.0 - q;
            }
            // 주사위 눈은 라운드 시작에 공개되므로 그 값을 그대로 쓴다.
            const int dice = view.marketDice;
            double expected = 0.0;
            for (int n = 0; n < kPlayerCount; ++n)
                expected += dist[n] * (dice / (n + 1));
            return expected;
        }

        double ReinforceValue(const PublicView& view, RegionKind kind, const Traits& traits)
        {
            const int myKnights = std::max(0, view.players[view.viewer].knights);
            const double units = kind == RegionKind::TrainingGround ? 1.0 : ExpectedMarketKnights(view);
            const bool low = myKnights <= kLowKnights;
            const double scale = low ? traits.lowKnightReinforceScale : traits.reinforceScale;
            // 마지막 라운드의 증원은 동점 처리에만 쓰이므로 보너스를 줄인다.
            const double bonus = low ? traits.lowKnightReinforceBonus * std::min(1.0, RemainingRounds(view.round) / 2.0)
                                     : 0.0;
            return units * KnightValue(myKnights, view.round) * scale + bonus;
        }

        struct Split
        {
            int first = 0;
            int second = 0;
            double utility = 0.0;
        };

        // 두 지역에 나눠 보내는 모든 경우(합 <= 보유 기사).
        std::vector<Split> EnumerateSplits(const Outlook& a, const Outlook& b, int knights)
        {
            std::vector<Split> splits;
            for (int c1 = 0; c1 <= a.maxSend; ++c1)
            {
                for (int c2 = 0; c2 <= b.maxSend && c1 + c2 <= knights; ++c2)
                    splits.push_back({c1, c2, a.utility[c1] + b.utility[c2]});
            }
            return splits;
        }

        // 기대 이득에 대한 소프트맥스 가중 무작위. 같은 상황에서도 결과가 달라진다.
        int PickWeighted(const std::vector<double>& utilities, double temperature, std::mt19937& rng)
        {
            const double best = *std::max_element(utilities.begin(), utilities.end());
            std::vector<double> weights;
            weights.reserve(utilities.size());
            for (double u : utilities)
                weights.push_back(u >= best - kPickCutoff * temperature ? std::exp((u - best) / temperature) : 0.0);
            std::discrete_distribution<int> dist(weights.begin(), weights.end());
            return dist(rng);
        }
    }

    AiPersonality GetAiPersonality(int player)
    {
        switch (player)
        {
        case 1: return AiPersonality::Aggressive;
        case 2: return AiPersonality::Cautious;
        case 3: return AiPersonality::Reinforcer;
        default: return AiPersonality::Balanced;
        }
    }

    Ai::Ai(AiPersonality personality, uint32_t seed)
        : m_personality(personality), m_rng(seed)
    {
    }

    FlagChoice Ai::ChooseFlags(const PublicView& view)
    {
        if (view.viewer < 0 || view.viewer >= kPlayerCount)
            return FlagChoice{4, 5};

        const Traits& traits = GetTraits(m_personality);
        const auto& defs = GetRegionDefs();
        const int myKnights = std::max(0, view.players[view.viewer].knights);

        // 지역마다 몬스터 기대 이득(배치는 나중에 정하므로 기사 수별로) 또는 증원 가치를 매긴다.
        std::array<Outlook, kRegionCount> outlooks{};
        std::array<double, kRegionCount> flagBonus{};
        for (int r = 0; r < kRegionCount; ++r)
        {
            if (defs[r].kind == RegionKind::Monster)
            {
                const OthersEstimate others = EstimateHidden(view, m_rng);
                outlooks[r] = EvaluateMonster(view, r, others, traits);
                const int score = view.monsterScores[r];
                if (outlooks[r].maxSend > 0 && score > 0)
                    flagBonus[r] = traits.bigMonsterBonus * score * std::min(1.0, static_cast<double>(myKnights) / score);
            }
            else
            {
                flagBonus[r] = ReinforceValue(view, defs[r].kind, traits);
            }
        }

        // 15가지 깃발 쌍마다 최선의 기사 분할 가치 + 지역 가치를 매기고 가중 무작위로 고른다.
        std::vector<FlagChoice> pairs;
        std::vector<double> utilities;
        for (int a = 0; a < kRegionCount; ++a)
        {
            for (int b = a + 1; b < kRegionCount; ++b)
            {
                double best = 0.0;
                for (const Split& s : EnumerateSplits(outlooks[a], outlooks[b], myKnights))
                    best = std::max(best, s.utility);
                pairs.push_back(FlagChoice{a, b});
                utilities.push_back(best + flagBonus[a] + flagBonus[b]);
            }
        }
        return pairs[PickWeighted(utilities, traits.flagTemperature, m_rng)];
    }

    KnightAllocation Ai::ChooseKnights(const PublicView& view)
    {
        KnightAllocation allocation{};
        if (view.viewer < 0 || view.viewer >= kPlayerCount)
            return allocation;
        const int myKnights = view.players[view.viewer].knights;
        if (myKnights <= 0)
            return allocation;

        const Traits& traits = GetTraits(m_personality);
        const auto& defs = GetRegionDefs();

        // 내 깃발이 있는 몬스터 지역만 후보(최대 2곳). 없으면 전부 0.
        std::array<int, kFlagsPerPlayer> regions{-1, -1};
        std::array<Outlook, kFlagsPerPlayer> outlooks{};
        int count = 0;
        for (int r = 0; r < kRegionCount && count < kFlagsPerPlayer; ++r)
        {
            if (!view.flags[view.viewer][r] || defs[r].kind != RegionKind::Monster)
                continue;
            regions[count] = r;
            outlooks[count] = EvaluateMonster(view, r, EstimateRevealed(view, r, m_rng), traits);
            ++count;
        }
        if (count == 0)
            return allocation;

        const std::vector<Split> splits = EnumerateSplits(outlooks[0], outlooks[1], myKnights);
        std::vector<double> utilities;
        utilities.reserve(splits.size());
        for (const Split& s : splits)
            utilities.push_back(s.utility);
        const Split& chosen = splits[PickWeighted(utilities, traits.knightTemperature, m_rng)];

        allocation[regions[0]] = chosen.first;
        if (regions[1] >= 0)
            allocation[regions[1]] = chosen.second;
        return allocation;
    }
}
