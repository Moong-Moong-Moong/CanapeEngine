#pragma once

#include <Engine.h>

#include "KnightsGame/KnightsAi.h"
#include "KnightsGame/KnightsRules.h"

#include <array>
#include <optional>

//***** 기사단 게임 본 화면
//*** 룰 코어(Knights::Game)와 AI를 소유하고, 코어 단계 위에 화면 단계(Stage)를 얹어 연출 시간을 준다.
//*** 코어는 제출이 모이면 곧바로 다음 단계로 넘어가므로, 공개·정산을 천천히 보여 주는 것은 이 스크립트의 몫이다.
class KnightsBoardController final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    KnightsBoardController() = default;

protected:
    void Start() override;
    void Update() override;
    void OnDestroy() override;

private:
    enum class Stage { PlayerFlags, AiFlags, FlagReveal, PlayerKnights, AiKnights, KnightReveal, Resolving, RoundEnd, GameOver };

    static constexpr float kAiTurnDelay = 0.5f;   // AI 한 명 차례 표시
    static constexpr float kRevealHold = 1.0f;    // 공개 후 잠깐 보여 주기
    static constexpr float kResolveStep = 1.2f;   // 정산 지역 하나
    static constexpr float kRoundEndHold = 1.0f;  // "라운드 n 종료" 표시
    static constexpr int kMaxKnightRows = Knights::kFlagsPerPlayer;

    //***** 지역 카드 한 장의 UI
    struct RegionCard
    {
        Canape::UIImage* frame = nullptr;     // 강조 테두리(카드 뒤)
        Canape::UIButton* button = nullptr;
        Canape::UIText* bigValue = nullptr;   // 몬스터 점수 / 증원 값
        std::array<Canape::UIImage*, Knights::kPlayerCount> flagChips{};
        std::array<Canape::UIElement*, Knights::kPlayerCount> knightChips{};
        std::array<Canape::UIText*, Knights::kPlayerCount> knightCounts{};
        Canape::UIText* result = nullptr;
    };

    //***** 점수판 한 줄
    struct ScoreRow
    {
        Canape::UIImage* background = nullptr;
        Canape::UIText* score = nullptr;
        Canape::UIText* knights = nullptr;
        Canape::UIText* status = nullptr;
    };

    //***** 기사 배치 한 줄: 지역 이름 [-] n [+]
    struct KnightRow
    {
        Canape::UIElement* root = nullptr;
        Canape::UIText* name = nullptr;
        Canape::UIButton* minus = nullptr;
        Canape::UIText* count = nullptr;
        Canape::UIButton* plus = nullptr;
        int region = -1;
    };

    //***** 화면 만들기
    void BuildTopBar(Canape::UIElement& root);
    void BuildBoard(Canape::UIElement& root);
    void BuildScoreboard(Canape::UIElement& root);
    void BuildActionPanel(Canape::UIElement& root);
    void BuildGameOverPanel(Canape::UIElement& root);

    //***** 진행
    void NewGame();
    void EnterStage(Stage stage);
    void AdvanceStage(bool skipRequested);
    void OnCardClicked(int region);
    void OnConfirmClicked();
    void ChangeAllocation(int row, int delta);
    bool ConsumeRawClick();

    //***** 매 프레임 화면 갱신
    void Refresh();
    void RefreshCards(const Knights::PublicView& view);
    void RefreshScoreboard(const Knights::PublicView& view);
    void RefreshActionPanel(const Knights::PublicView& view);
    void RefreshGameOver();

    // 정산 중에는 아직 보여 주지 않은 이벤트를 빼서, 화면 숫자가 보여 준 이벤트까지만 반영되게 한다.
    std::array<Knights::PlayerState, Knights::kPlayerCount> GetShownPlayers(const Knights::PublicView& view) const;
    // 지금까지 화면에 보여 준 정산 이벤트 수
    int GetShownEventCount() const;
    int GetCurrentTurnPlayer() const;
    int GetSelectedFlagCount() const;
    int GetAllocatedKnights() const;

    Knights::Game m_Game{ 0 };
    std::array<std::optional<Knights::Ai>, Knights::kPlayerCount> m_Ais;  // 1~4만 사용
    Stage m_Stage = Stage::PlayerFlags;
    float m_Timer = 0.0f;
    int m_AiIndex = 1;       // AiFlags / AiKnights에서 다음에 둘 AI
    int m_ResolveIndex = 0;  // Resolving에서 지금 보여 주는 이벤트
    std::array<bool, Knights::kRegionCount> m_SelectedFlags{};
    Knights::KnightAllocation m_Allocation{};
    bool m_MouseWasDown = true;  // 원본 마우스 누름 검출용(씬에 들어온 클릭은 무시하도록 true로 시작)

    Canape::TextureResourceId m_RoundedTexture;
    Canape::UIText* m_RoundText = nullptr;
    Canape::UIText* m_StageText = nullptr;
    std::array<RegionCard, Knights::kRegionCount> m_Cards{};
    std::array<ScoreRow, Knights::kPlayerCount> m_ScoreRows{};
    Canape::UIText* m_MessageText = nullptr;
    Canape::UIText* m_DetailText = nullptr;
    std::array<KnightRow, kMaxKnightRows> m_KnightRows{};
    Canape::UIText* m_RemainingText = nullptr;
    Canape::UIButton* m_ConfirmButton = nullptr;
    Canape::UIText* m_ConfirmLabel = nullptr;
    Canape::UIElement* m_GameOverRoot = nullptr;
    Canape::UIText* m_WinnerText = nullptr;
    std::array<Canape::UIImage*, Knights::kPlayerCount> m_RankChips{};
    std::array<Canape::UIText*, Knights::kPlayerCount> m_RankNames{};
    std::array<Canape::UIText*, Knights::kPlayerCount> m_RankScores{};
    std::array<Canape::UIText*, Knights::kPlayerCount> m_RankKnights{};
    std::array<Canape::UIImage*, Knights::kPlayerCount> m_RankBackgrounds{};
};
