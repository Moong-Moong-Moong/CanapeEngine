#pragma once

#include <Engine.h>

#include "KnightsGame/KnightsAi.h"
#include "KnightsGame/KnightsRules.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

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
        Canape::UIText* sub = nullptr;        // 종류·점수 범위 줄(몬스터 범위는 라운드에 따라 바뀐다)
        std::array<Canape::UIImage*, Knights::kPlayerCount> flagChips{};
        std::array<Canape::UIElement*, Knights::kPlayerCount> knightChips{};
        std::array<Canape::UIText*, Knights::kPlayerCount> knightCounts{};
        Canape::UIText* result = nullptr;     // 결과 줄(잘라 내는 상자 안에 있어 카드 밖으로 넘치지 않는다)
        float resultFitScale = 0.0f;          // 결과 줄 글자 크기를 마지막으로 맞춘 레이아웃 배율
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
    void GoToTitle();

    //***** 제출과 단계 확인. 코어가 받아들인 경우에만 다음으로 넘어간다.
    bool SubmitAiFlags(int ai);
    bool SubmitAiKnights(int ai);
    // 코어 단계가 기대와 다르면 진행을 멈추고(m_Stalled) 화면과 로그에 알린다.
    bool RequireCorePhase(Knights::Phase expected, const char* where);
    void ReportProblem(const std::string& text);
    void Stall(const std::string& text);
    // 증원(몬스터가 아닌) 지역 2곳. 항상 서로 다른 유효 지역이다.
    static Knights::FlagChoice GetFallbackFlags();

    //***** 매 프레임 화면 갱신
    void Refresh();
    void RefreshCards(const Knights::PublicView& view);
    void RefreshScoreboard(const Knights::PublicView& view);
    void RefreshActionPanel(const Knights::PublicView& view);
    // 결과 줄 글자를 바꾸고, 상자(카드 안)에 다 들어가도록 글자 크기를 줄인다.
    static void SetCardResult(RegionCard& card, const std::string& text, Canape::UIColor color);
    // 종료 화면 내용(순위·승자)은 GameOver에 들어갈 때 한 번만 채운다.
    void ShowGameOver();

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
    std::string m_ProblemText;   // 마지막 제출 거부·진행 오류(화면 하단에 표시, 단계가 바뀌면 지움)
    bool m_Stalled = false;      // 복구할 수 없는 진행 오류: 자동 진행을 멈춘다
    // 지난 라운드 정산 결과의 사본. 코어는 다음 라운드로 넘어가며 기록을 비우므로 화면이 따로 들고,
    // 다음 라운드의 깃발·기사 배치 동안 카드에 흐리게 보여 준다. 새 결과가 나오면(기사 공개) 그만 보이고, 새 게임에서 지운다.
    std::vector<Knights::RegionEvent> m_LastEvents;

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
    Canape::UIElement* m_StallButtons = nullptr;  // 진행 오류로 멈췄을 때만 보이는 [다시 하기] [타이틀로]
    Canape::UIElement* m_GameOverRoot = nullptr;
    Canape::UIText* m_WinnerText = nullptr;
    std::array<Canape::UIText*, Knights::kPlayerCount> m_RankPlaces{};
    std::array<Canape::UIImage*, Knights::kPlayerCount> m_RankChips{};
    std::array<Canape::UIText*, Knights::kPlayerCount> m_RankNames{};
    std::array<Canape::UIText*, Knights::kPlayerCount> m_RankScores{};
    std::array<Canape::UIText*, Knights::kPlayerCount> m_RankKnights{};
    std::array<Canape::UIImage*, Knights::kPlayerCount> m_RankBackgrounds{};
};
