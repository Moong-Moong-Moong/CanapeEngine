#include "KnightsGame/KnightsBoardText.h"
#include "KnightsGame/KnightsAi.h"

#include <vector>

namespace Knights
{
    namespace
    {
        std::string Join(const std::vector<std::string>& parts)
        {
            std::string result;
            for (size_t i = 0; i < parts.size(); ++i)
            {
                if (i > 0)
                    result += ", ";
                result += parts[i];
            }
            return result;
        }

        std::string FlaggedNames(const RegionEvent& event)
        {
            std::vector<std::string> names;
            for (int p = 0; p < kPlayerCount; ++p)
            {
                if (event.flagged[p])
                    names.push_back(PlayerName(p));
            }
            return Join(names);
        }

        int CountFlagged(const RegionEvent& event)
        {
            int count = 0;
            for (bool flagged : event.flagged)
            {
                if (flagged)
                    ++count;
            }
            return count;
        }

        EventText DescribeMonster(const RegionEvent& event, const std::string& name)
        {
            EventText text;
            const std::string versus = "기사 " + std::to_string(event.totalKnights) + "명 vs 몬스터 " +
                                       std::to_string(event.monsterScore) + "점";
            if (CountFlagged(event) == 0)
            {
                text.headline = name + " — 깃발 없음";
                text.detail = "아무도 깃발을 꽂지 않았습니다.";
                text.cardLine = "깃발 없음";
                return text;
            }
            if (event.totalKnights == 0)
            {
                text.headline = name + " — 아무도 싸우지 않음";
                text.detail = "깃발만 꽂고 기사를 보내지 않았습니다.";
                text.cardLine = "싸운 기사 없음";
                return text;
            }
            if (event.wiped)
            {
                std::vector<std::string> losses;
                std::vector<std::string> shortLosses;
                for (int p = 0; p < kPlayerCount; ++p)
                {
                    if (event.knightsLost[p] > 0)
                    {
                        losses.push_back(PlayerName(p) + " 기사 " + std::to_string(event.knightsLost[p]) + "명");
                        shortLosses.push_back(PlayerName(p) + " -" + std::to_string(event.knightsLost[p]));
                    }
                }
                text.headline = name + " 전멸 — " + Join(losses) + " 잃음";
                text.detail = "보낸 기사 합 " + std::to_string(event.totalKnights) + "명이 몬스터 점수 " +
                              std::to_string(event.monsterScore) + "점보다 적어 모두 쓰러졌습니다. 점수는 아무도 얻지 못합니다.";
                text.cardLine = "전멸 — " + versus + "\n기사 잃음: " + Join(shortLosses);
                text.tone = EventTone::Wipe;
                return text;
            }

            std::vector<std::string> gains;
            for (int p = 0; p < kPlayerCount; ++p)
            {
                if (event.sent[p] > 0)
                    gains.push_back(PlayerName(p) + " +" + std::to_string(event.scoreGained[p]));
            }
            std::string discarded;
            if (event.discarded > 0)
                discarded = " (" + std::to_string(event.discarded) + "점 폐기)";
            text.headline = name + " 승리! " + Join(gains) + discarded;
            text.detail = "기사 합 " + std::to_string(event.totalKnights) + "명이 몬스터 점수 " +
                          std::to_string(event.monsterScore) +
                          "점 이상이라 승리. 적게 보낸 쪽부터 보낸 수만큼 점수를 가져가고, 기사는 모두 돌아옵니다.";
            text.cardLine = "승리 — " + versus + "\n" + Join(gains) + discarded;
            text.tone = EventTone::Win;
            return text;
        }

        EventText DescribeTraining(const RegionEvent& event, const std::string& name)
        {
            EventText text;
            if (CountFlagged(event) == 0)
            {
                text.headline = name + " — 깃발 없음";
                text.detail = "아무도 깃발을 꽂지 않았습니다.";
                text.cardLine = "깃발 없음";
                return text;
            }
            const std::string names = FlaggedNames(event);
            text.headline = name + " — " + names + " 기사 +1";
            text.detail = "깃발 1개당 기사 1명을 받습니다. 받은 기사는 다음 라운드부터 보낼 수 있습니다.";
            text.cardLine = "기사 +1\n" + names;
            text.tone = EventTone::Reinforce;
            return text;
        }

        EventText DescribeMarket(const RegionEvent& event, const std::string& name)
        {
            EventText text;
            const int flagged = CountFlagged(event);
            if (flagged == 0)
            {
                text.headline = name + " — 깃발 없음";
                text.detail = "아무도 깃발을 꽂지 않아 주사위를 굴리지 않았습니다.";
                text.cardLine = "깃발 없음";
                return text;
            }
            const int share = event.dice / flagged;
            std::string discarded;
            if (event.discarded > 0)
                discarded = " (" + std::to_string(event.discarded) + "명 폐기)";
            const std::string split = std::to_string(flagged) + "명이 " + std::to_string(share) + "명씩";
            text.headline = name + " 주사위 " + std::to_string(event.dice) + " → " + split + discarded;
            text.detail = "받는 사람: " + FlaggedNames(event) +
                          ". 주사위 눈을 깃발 수로 나누고, 받은 기사는 다음 라운드부터 보낼 수 있습니다.";
            text.cardLine = "주사위 " + std::to_string(event.dice) + " → " + split + "\n" + FlaggedNames(event);
            text.tone = EventTone::Reinforce;
            return text;
        }
    }

    std::string PlayerName(int player)
    {
        if (player == kHumanPlayer)
            return "나";
        return "AI " + std::to_string(player);
    }

    std::string PersonalityName(int player)
    {
        if (player == kHumanPlayer)
            return {};
        switch (GetAiPersonality(player))
        {
        case AiPersonality::Aggressive: return "공격형";
        case AiPersonality::Cautious: return "신중형";
        case AiPersonality::Reinforcer: return "증원형";
        case AiPersonality::Balanced: return "균형형";
        }
        return {};
    }

    EventText DescribeEvent(const RegionEvent& event)
    {
        const std::string name(GetRegionDefs()[event.region].name);
        switch (event.kind)
        {
        case RegionKind::Monster: return DescribeMonster(event, name);
        case RegionKind::TrainingGround: return DescribeTraining(event, name);
        case RegionKind::MercenaryMarket: return DescribeMarket(event, name);
        }
        return {};
    }
}
