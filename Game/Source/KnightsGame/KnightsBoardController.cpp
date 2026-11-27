#include "KnightsGame/KnightsBoardController.h"
#include "KnightsGame/KnightsBoardText.h"
#include "KnightsGame/KnightsUi.h"

#include <algorithm>
#include <random>
#include <string>
#include <vector>

RTTR_REGISTRATION
{
    rttr::registration::class_<KnightsBoardController>("KnightsBoardController")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

using namespace Canape;
using namespace Knights;

namespace
{
    //***** 배치(캔버스 1920×1080 기준 픽셀)
    constexpr float kMargin = 24.0f;
    constexpr float kTopBarHeight = 84.0f;
    constexpr float kBoardTop = 120.0f;
    constexpr float kCardWidth = 448.0f;
    constexpr float kCardHeight = 300.0f;
    constexpr float kCardGapX = 28.0f;
    constexpr float kCardGapY = 20.0f;
    constexpr float kFrameThickness = 6.0f;
    constexpr float kScoreboardX = kMargin + 3.0f * kCardWidth + 2.0f * kCardGapX + kMargin;
    constexpr float kScoreboardWidth = 1920.0f - kMargin - kScoreboardX;
    constexpr float kBoardBottom = kBoardTop + 2.0f * kCardHeight + kCardGapY;
    constexpr float kActionTop = kBoardBottom + 20.0f;
    constexpr float kActionHeight = 1080.0f - kMargin - kActionTop;
    constexpr float kScoreRowTop = 96.0f;
    constexpr float kScoreRowHeight = 96.0f;
    constexpr float kScoreRowStep = 104.0f;

    //***** 색
    constexpr UIColor kMonsterCardColor{ 96, 62, 66 };
    constexpr UIColor kMonsterCardActiveColor{ 150, 92, 96 };
    constexpr UIColor kReinforceCardColor{ 48, 82, 98 };
    constexpr UIColor kReinforceCardActiveColor{ 72, 124, 146 };
    constexpr UIColor kSelectFrameColor = KnightsUi::kTitleColor;
    constexpr UIColor kResolveFrameColor{ 245, 245, 250 };
    constexpr UIColor kScoreRowColor{ 46, 53, 72 };
    constexpr UIColor kScoreRowTurnColor{ 92, 102, 140 };
    constexpr UIColor kRankRowColor{ 48, 54, 72 };
    constexpr UIColor kRankWinnerColor{ 104, 86, 40 };
    constexpr UIColor kOverlayColor{ 0, 0, 0, 170 };

    // 카드는 비활성일 때도 흐려지지 않게 하고, 활성일 때는 올렸을 때 밝아지게 한다.
    constexpr UIButtonColors kCardStateColors{ { 225, 225, 225, 255 }, { 255, 255, 255, 255 }, { 196, 196, 196, 255 }, { 225, 225, 225, 255 } };
    // 행동 버튼은 비활성이 분명히 보이게 회색으로.
    constexpr UIButtonColors kActionStateColors{ { 255, 255, 255, 255 }, { 225, 225, 225, 255 }, { 190, 190, 190, 255 }, { 110, 110, 110, 255 } };

    std::string Num(int value)
    {
        return std::to_string(value);
    }

    void SetButtonEnabled(UIButton& button, bool enabled)
    {
        button.SetInteractable(enabled);
        if (auto* label = button.Find<UIText>("Label"))
        {
            label->SetColor(enabled ? KnightsUi::kTextColor : KnightsUi::kSubTextColor);
        }
    }

    using KnightsUi::AddLabel;
}

//***** 수명

void KnightsBoardController::Start()
{
    m_RoundedTexture = KnightsUi::CreateRoundedTexture();

    auto* canvas = AddComponent<UICanvasComponent>(Vector2{ 1920.0f, 1080.0f });
    UIElement& root = canvas->GetRoot();

    KnightsUi::AddBackground(root);
    BuildTopBar(root);
    BuildBoard(root);
    BuildScoreboard(root);
    BuildActionPanel(root);
    BuildGameOverPanel(root);

    NewGame();
    Refresh();

    Application::Get().GetInputManager().SetCursorRequested(true);
}

void KnightsBoardController::Update()
{
    // 씬 언로드가 지연되어 이전 씬의 OnDestroy(false)가 늦게 올 수 있으므로 매 프레임 다시 요청한다.
    Application::Get().GetInputManager().SetCursorRequested(true);

    const bool clicked = ConsumeRawClick();
    m_Timer += Time::DeltaTime();
    AdvanceStage(clicked);
    Refresh();
}

void KnightsBoardController::OnDestroy()
{
    Application::Get().GetInputManager().SetCursorRequested(false);
    KnightsUi::DestroyTexture(m_RoundedTexture);
}

//***** 화면 만들기

void KnightsBoardController::BuildTopBar(UIElement& root)
{
    auto* bar = KnightsUi::AddPanel(root, "TopBar", m_RoundedTexture, { kMargin, 16.0f }, { 1920.0f - 2.0f * kMargin, kTopBarHeight },
                                    KnightsUi::kPanelColor);
    m_RoundText = AddLabel(*bar, "Round", "", 44.0f, { 28.0f, 0.0f }, { 300.0f, kTopBarHeight }, KnightsUi::kTitleColor);
    m_StageText = AddLabel(*bar, "Stage", "", 32.0f, { 330.0f, 0.0f }, { 1500.0f, kTopBarHeight });
}

void KnightsBoardController::BuildBoard(UIElement& root)
{
    const auto& defs = GetRegionDefs();
    for (int r = 0; r < kRegionCount; ++r)
    {
        const RegionDef& def = defs[r];
        const bool monster = def.kind == RegionKind::Monster;
        const Vector2 position{ kMargin + (r % 3) * (kCardWidth + kCardGapX), kBoardTop + (r / 3) * (kCardHeight + kCardGapY) };
        RegionCard& card = m_Cards[r];

        //*** 강조 테두리: 카드보다 조금 큰 둥근 사각형을 카드 뒤에 둔다.
        card.frame = KnightsUi::AddPanel(root, "CardFrame" + Num(r), m_RoundedTexture,
                                         { position.X - kFrameThickness, position.Y - kFrameThickness },
                                         { kCardWidth + 2.0f * kFrameThickness, kCardHeight + 2.0f * kFrameThickness },
                                         kSelectFrameColor, 20.0f);
        card.frame->SetVisible(false);

        card.button = root.AddChild<UIButton>("Card" + Num(r));
        card.button->SetPosition(position);
        card.button->SetSize({ kCardWidth, kCardHeight });
        KnightsUi::MakeRounded(*card.button, m_RoundedTexture, 16.0f);
        card.button->SetStateColors(kCardStateColors);
        card.button->OnClicked = [this, r] { OnCardClicked(r); };

        UIButton& face = *card.button;
        AddLabel(face, "Name", std::string(def.name), 34.0f, { 20.0f, 10.0f }, { 260.0f, 48.0f });

        std::string sub;
        if (monster)
            sub = "몬스터 · 점수 범위 " + Num(def.minScore) + "~" + Num(def.maxScore);
        else if (def.kind == RegionKind::TrainingGround)
            sub = "증원 · 깃발 1개당 기사 +1";
        else
            sub = "증원 · 주사위 → 깃발 수로 나눔";
        AddLabel(face, "Sub", sub, 22.0f, { 20.0f, 64.0f }, { 410.0f, 32.0f }, KnightsUi::kSubTextColor);

        card.bigValue = AddLabel(face, "Value", "", monster ? 56.0f : 38.0f, { -20.0f, 4.0f }, { 180.0f, 62.0f },
                                 KnightsUi::kTitleColor, UITextAlign::End);
        card.bigValue->SetAnchor({ 1.0f, 0.0f });
        card.bigValue->SetPivot({ 1.0f, 0.0f });

        //*** 깃발 칩 줄
        AddLabel(face, "FlagLabel", "깃발", 22.0f, { 20.0f, 112.0f }, { 60.0f, 38.0f }, KnightsUi::kSubTextColor);
        auto* flagBox = face.AddChild<UIHorizontalBox>("Flags");
        flagBox->SetPosition({ 84.0f, 112.0f });
        flagBox->SetSize({ 344.0f, 38.0f });
        flagBox->SetSpacing(10.0f);
        flagBox->SetChildAlign(UIBoxAlign::Center);
        for (int p = 0; p < kPlayerCount; ++p)
        {
            card.flagChips[p] = KnightsUi::AddPanel(*flagBox, "Flag" + Num(p), m_RoundedTexture, {}, { 30.0f, 30.0f },
                                                    KnightsUi::kPlayerColors[p], 6.0f);
        }

        //*** 기사 칩 줄: 색 사각형 + 숫자
        AddLabel(face, "KnightLabel", "기사", 22.0f, { 20.0f, 160.0f }, { 60.0f, 40.0f }, KnightsUi::kSubTextColor);
        auto* knightBox = face.AddChild<UIHorizontalBox>("Knights");
        knightBox->SetPosition({ 84.0f, 160.0f });
        knightBox->SetSize({ 344.0f, 40.0f });
        knightBox->SetSpacing(8.0f);
        knightBox->SetChildAlign(UIBoxAlign::Center);
        for (int p = 0; p < kPlayerCount; ++p)
        {
            auto* chip = knightBox->AddChild<UIElement>("Knight" + Num(p));
            chip->SetSize({ 60.0f, 40.0f });
            KnightsUi::AddPanel(*chip, "Square", m_RoundedTexture, { 0.0f, 7.0f }, { 26.0f, 26.0f }, KnightsUi::kPlayerColors[p], 6.0f);
            card.knightCounts[p] = AddLabel(*chip, "Count", "", 28.0f, { 30.0f, 0.0f }, { 30.0f, 40.0f });
            card.knightChips[p] = chip;
        }

        //*** 결과 줄(정산 뒤 라운드 끝까지 남는다)
        card.result = AddLabel(face, "Result", "", 22.0f, { 20.0f, 210.0f }, { 410.0f, 80.0f }, KnightsUi::kSubTextColor,
                               UITextAlign::Start, UITextAlign::Start);
        card.result->SetWrap(true);
    }
}

void KnightsBoardController::BuildScoreboard(UIElement& root)
{
    auto* board = KnightsUi::AddPanel(root, "Scoreboard", m_RoundedTexture, { kScoreboardX, kBoardTop },
                                      { kScoreboardWidth, kBoardBottom - kBoardTop }, KnightsUi::kPanelColor);
    AddLabel(*board, "Title", "점수판", 32.0f, { 24.0f, 12.0f }, { 200.0f, 48.0f }, KnightsUi::kTitleColor);
    AddLabel(*board, "ScoreHeader", "점수", 22.0f, { 198.0f, 60.0f }, { 96.0f, 32.0f }, KnightsUi::kSubTextColor, UITextAlign::Center);
    AddLabel(*board, "KnightHeader", "기사", 22.0f, { 294.0f, 60.0f }, { 72.0f, 32.0f }, KnightsUi::kSubTextColor, UITextAlign::Center);

    for (int p = 0; p < kPlayerCount; ++p)
    {
        ScoreRow& row = m_ScoreRows[p];
        row.background = KnightsUi::AddPanel(*board, "Row" + Num(p), m_RoundedTexture, { 12.0f, kScoreRowTop + p * kScoreRowStep },
                                             { kScoreboardWidth - 24.0f, kScoreRowHeight }, kScoreRowColor, 12.0f);
        UIImage& bg = *row.background;
        KnightsUi::AddPanel(bg, "Color", m_RoundedTexture, { 14.0f, 30.0f }, { 36.0f, 36.0f }, KnightsUi::kPlayerColors[p], 8.0f);
        const std::string personality = PersonalityName(p);
        if (personality.empty())
        {
            AddLabel(bg, "Name", PlayerName(p), 32.0f, { 62.0f, 0.0f }, { 120.0f, kScoreRowHeight });
        }
        else
        {
            AddLabel(bg, "Name", PlayerName(p), 32.0f, { 62.0f, 8.0f }, { 120.0f, 44.0f });
            AddLabel(bg, "Personality", personality, 20.0f, { 62.0f, 54.0f }, { 120.0f, 30.0f }, KnightsUi::kSubTextColor);
        }
        row.score = AddLabel(bg, "Score", "", 34.0f, { 186.0f, 0.0f }, { 96.0f, kScoreRowHeight }, KnightsUi::kTextColor, UITextAlign::Center);
        row.knights = AddLabel(bg, "Knights", "", 30.0f, { 282.0f, 0.0f }, { 72.0f, kScoreRowHeight }, KnightsUi::kTextColor, UITextAlign::Center);
        row.status = AddLabel(bg, "Status", "", 24.0f, { 352.0f, 0.0f }, { kScoreboardWidth - 24.0f - 356.0f, kScoreRowHeight },
                              KnightsUi::kGoodColor, UITextAlign::Center);
    }
}

void KnightsBoardController::BuildActionPanel(UIElement& root)
{
    auto* panel = KnightsUi::AddPanel(root, "ActionPanel", m_RoundedTexture, { kMargin, kActionTop },
                                      { 1920.0f - 2.0f * kMargin, kActionHeight }, KnightsUi::kPanelColor);
    m_MessageText = AddLabel(*panel, "Message", "", 38.0f, { 28.0f, 12.0f }, { 1490.0f, 56.0f });
    m_DetailText = AddLabel(*panel, "Detail", "", 26.0f, { 28.0f, 72.0f }, { 1490.0f, 72.0f }, KnightsUi::kSubTextColor,
                            UITextAlign::Start, UITextAlign::Start);
    m_DetailText->SetWrap(true);

    for (int i = 0; i < kMaxKnightRows; ++i)
    {
        KnightRow& row = m_KnightRows[i];
        row.root = panel->AddChild<UIElement>("KnightRow" + Num(i));
        row.root->SetPosition({ 28.0f + i * 560.0f, 150.0f });
        row.root->SetSize({ 480.0f, 100.0f });
        row.name = AddLabel(*row.root, "Name", "", 32.0f, { 0.0f, 0.0f }, { 220.0f, 100.0f });
        row.minus = KnightsUi::AddButton(*row.root, "Minus", m_RoundedTexture, "-", 48.0f, { 226.0f, 14.0f }, { 72.0f, 72.0f },
                                         KnightsUi::kNeutralButtonColor);
        row.minus->SetStateColors(kActionStateColors);
        row.minus->OnClicked = [this, i] { ChangeAllocation(i, -1); };
        row.count = AddLabel(*row.root, "Count", "0", 44.0f, { 302.0f, 0.0f }, { 84.0f, 100.0f }, KnightsUi::kTitleColor,
                             UITextAlign::Center);
        row.plus = KnightsUi::AddButton(*row.root, "Plus", m_RoundedTexture, "+", 48.0f, { 390.0f, 14.0f }, { 72.0f, 72.0f },
                                        KnightsUi::kNeutralButtonColor);
        row.plus->SetStateColors(kActionStateColors);
        row.plus->OnClicked = [this, i] { ChangeAllocation(i, +1); };
        row.root->SetVisible(false);
    }

    m_RemainingText = AddLabel(*panel, "Remaining", "", 32.0f, { 1150.0f, 150.0f }, { 340.0f, 100.0f }, KnightsUi::kTextColor,
                               UITextAlign::End);

    m_ConfirmButton = KnightsUi::AddButton(*panel, "Confirm", m_RoundedTexture, "확정", 44.0f,
                                           { 1920.0f - 2.0f * kMargin - 28.0f - 300.0f, kActionHeight - 28.0f - 110.0f },
                                           { 300.0f, 110.0f }, KnightsUi::kConfirmColor);
    m_ConfirmButton->SetStateColors(kActionStateColors);
    m_ConfirmButton->OnClicked = [this] { OnConfirmClicked(); };
}

void KnightsBoardController::BuildGameOverPanel(UIElement& root)
{
    //*** 화면 전체를 어둡게 덮어 아래 보드 입력을 막는다.
    auto* overlay = root.AddChild<UIImage>("GameOver");
    overlay->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    overlay->SetSize({ 0.0f, 0.0f });
    overlay->SetColor(kOverlayColor);
    overlay->SetHitTestable(true);
    m_GameOverRoot = overlay;

    constexpr float width = 860.0f;
    constexpr float height = 720.0f;
    auto* panel = KnightsUi::AddPanel(*overlay, "Panel", m_RoundedTexture, { 0.0f, 0.0f }, { width, height }, KnightsUi::kPanelColor, 20.0f);
    panel->SetAnchor({ 0.5f, 0.5f });
    panel->SetPivot({ 0.5f, 0.5f });

    m_WinnerText = AddLabel(*panel, "Winner", "", 56.0f, { 0.0f, 28.0f }, { width, 80.0f }, KnightsUi::kTitleColor, UITextAlign::Center);
    AddLabel(*panel, "Subtitle", "최종 순위 (점수, 같으면 남은 기사)", 26.0f, { 0.0f, 108.0f }, { width, 40.0f },
             KnightsUi::kSubTextColor, UITextAlign::Center);

    for (int i = 0; i < kPlayerCount; ++i)
    {
        m_RankBackgrounds[i] = KnightsUi::AddPanel(*panel, "Rank" + Num(i), m_RoundedTexture, { 60.0f, 160.0f + i * 78.0f },
                                                   { width - 120.0f, 68.0f }, kRankRowColor, 12.0f);
        UIImage& row = *m_RankBackgrounds[i];
        AddLabel(row, "Place", "", 30.0f, { 20.0f, 0.0f }, { 70.0f, 68.0f }, KnightsUi::kSubTextColor);
        m_RankChips[i] = KnightsUi::AddPanel(row, "Color", m_RoundedTexture, { 96.0f, 18.0f }, { 32.0f, 32.0f }, KnightsUi::kPlayerColors[0], 8.0f);
        m_RankNames[i] = AddLabel(row, "Name", "", 34.0f, { 146.0f, 0.0f }, { 220.0f, 68.0f });
        m_RankScores[i] = AddLabel(row, "Score", "", 34.0f, { 360.0f, 0.0f }, { 160.0f, 68.0f }, KnightsUi::kTitleColor, UITextAlign::End);
        m_RankKnights[i] = AddLabel(row, "Knights", "", 30.0f, { 530.0f, 0.0f }, { 170.0f, 68.0f }, KnightsUi::kTextColor, UITextAlign::End);
    }

    auto* again = KnightsUi::AddButton(*panel, "Again", m_RoundedTexture, "다시 하기", 36.0f, { 130.0f, height - 120.0f },
                                       { 280.0f, 90.0f }, KnightsUi::kConfirmColor);
    again->OnClicked = [this] { NewGame(); };

    auto* title = KnightsUi::AddButton(*panel, "ToTitle", m_RoundedTexture, "타이틀로", 36.0f, { width - 130.0f - 280.0f, height - 120.0f },
                                       { 280.0f, 90.0f }, KnightsUi::kNeutralButtonColor);
    title->OnClicked = [this]
    {
        SceneManager& sceneManager = Application::Get().GetSceneManager();
        Scene* currentScene = GetOwnerObject()->GetScene();
        if (sceneManager.OpenScene("KnightsTitle"))
        {
            sceneManager.UnloadScene(currentScene);
        }
    };

    overlay->SetVisible(false);
}

//***** 진행

void KnightsBoardController::NewGame()
{
    std::random_device device;
    m_Game.Reset(device());
    for (int p = 1; p < kPlayerCount; ++p)
    {
        m_Ais[p].emplace(GetAiPersonality(p), device());
    }
    // 카드 결과 줄·칩·점수판은 매 프레임 Refresh가 새 상태로 다시 그린다.
    EnterStage(Stage::PlayerFlags);
}

void KnightsBoardController::EnterStage(Stage stage)
{
    m_Stage = stage;
    m_Timer = 0.0f;
    switch (stage)
    {
    case Stage::PlayerFlags:
        m_SelectedFlags = {};
        m_Allocation = {};
        break;
    case Stage::AiFlags:
    case Stage::AiKnights:
        m_AiIndex = 1;
        break;
    case Stage::PlayerKnights:
    {
        //*** 내 깃발이 있는 몬스터 지역마다 배치 줄 하나
        m_Allocation = {};
        const PublicView view = m_Game.GetView(kHumanPlayer);
        const auto& defs = GetRegionDefs();
        int row = 0;
        for (int r = 0; r < kRegionCount && row < kMaxKnightRows; ++r)
        {
            if (view.flags[kHumanPlayer][r] && defs[r].kind == RegionKind::Monster)
            {
                m_KnightRows[row].region = r;
                m_KnightRows[row].name->SetText(std::string(defs[r].name));
                ++row;
            }
        }
        for (; row < kMaxKnightRows; ++row)
        {
            m_KnightRows[row].region = -1;
        }
        break;
    }
    case Stage::Resolving:
        m_ResolveIndex = 0;
        break;
    default:
        break;
    }
}

void KnightsBoardController::AdvanceStage(bool skipRequested)
{
    switch (m_Stage)
    {
    case Stage::AiFlags:
        if (m_Timer >= kAiTurnDelay)
        {
            const int ai = m_AiIndex;
            // AI는 항상 유효한 선택을 돌려주지만, 혹시 거부되면 증원 두 곳으로 대신해 진행이 멈추지 않게 한다.
            if (m_Game.SubmitFlags(ai, m_Ais[ai]->ChooseFlags(m_Game.GetView(ai))) != SubmitError::None)
            {
                m_Game.SubmitFlags(ai, { 4, 5 });
            }
            m_Timer = 0.0f;
            if (++m_AiIndex >= kPlayerCount)
                EnterStage(Stage::FlagReveal);
        }
        break;
    case Stage::FlagReveal:
        if (m_Timer >= kRevealHold)
            EnterStage(Stage::PlayerKnights);
        break;
    case Stage::AiKnights:
        if (m_Timer >= kAiTurnDelay)
        {
            const int ai = m_AiIndex;
            // 혹시 거부되면 0명 배치(항상 유효)로 대신한다.
            if (m_Game.SubmitKnights(ai, m_Ais[ai]->ChooseKnights(m_Game.GetView(ai))) != SubmitError::None)
            {
                m_Game.SubmitKnights(ai, KnightAllocation{});
            }
            m_Timer = 0.0f;
            if (++m_AiIndex >= kPlayerCount)
                EnterStage(Stage::KnightReveal);
        }
        break;
    case Stage::KnightReveal:
        if (m_Timer >= kRevealHold)
            EnterStage(Stage::Resolving);
        break;
    case Stage::Resolving:
        if (m_Timer >= kResolveStep || skipRequested)
        {
            m_Timer = 0.0f;
            if (++m_ResolveIndex >= static_cast<int>(m_Game.GetResolution().size()))
                EnterStage(Stage::RoundEnd);
        }
        break;
    case Stage::RoundEnd:
        if (m_Timer >= kRoundEndHold || skipRequested)
        {
            m_Game.AdvanceRound();
            EnterStage(m_Game.GetPhase() == Phase::GameOver ? Stage::GameOver : Stage::PlayerFlags);
        }
        break;
    default:
        break;
    }
}

void KnightsBoardController::OnCardClicked(int region)
{
    if (m_Stage != Stage::PlayerFlags)
        return;
    if (m_SelectedFlags[region])
        m_SelectedFlags[region] = false;
    else if (GetSelectedFlagCount() < kFlagsPerPlayer)
        m_SelectedFlags[region] = true;
}

void KnightsBoardController::OnConfirmClicked()
{
    if (m_Stage == Stage::PlayerFlags)
    {
        if (GetSelectedFlagCount() != kFlagsPerPlayer)
            return;
        FlagChoice choice{};
        int n = 0;
        for (int r = 0; r < kRegionCount; ++r)
        {
            if (m_SelectedFlags[r])
                choice[n++] = r;
        }
        if (m_Game.SubmitFlags(kHumanPlayer, choice) == SubmitError::None)
            EnterStage(Stage::AiFlags);
    }
    else if (m_Stage == Stage::PlayerKnights)
    {
        // 0명 배치도 그대로 제출한다(깃발이 증원 지역에만 있거나 기사가 없어도 막히지 않음).
        if (m_Game.SubmitKnights(kHumanPlayer, m_Allocation) == SubmitError::None)
            EnterStage(Stage::AiKnights);
    }
}

void KnightsBoardController::ChangeAllocation(int row, int delta)
{
    if (m_Stage != Stage::PlayerKnights)
        return;
    const int region = m_KnightRows[row].region;
    if (region < 0)
        return;
    const int next = m_Allocation[region] + delta;
    if (next < 0 || GetAllocatedKnights() + delta > m_Game.GetPlayer(kHumanPlayer).knights)
        return;
    m_Allocation[region] = next;
}

bool KnightsBoardController::ConsumeRawClick()
{
    // UI가 쓴 클릭은 IsMousePressed에서 빠지므로, "화면 아무 곳이나 클릭"은 원본 상태로 직접 검출한다.
    const bool down = Application::Get().GetInputManager().GetInput().MouseButtons[static_cast<size_t>(MouseButton::Left)];
    const bool pressed = down && !m_MouseWasDown;
    m_MouseWasDown = down;
    return pressed;
}

//***** 조회

int KnightsBoardController::GetShownEventCount() const
{
    if (m_Game.GetPhase() != Phase::Resolution)
        return 0;
    const int total = static_cast<int>(m_Game.GetResolution().size());
    if (m_Stage == Stage::Resolving)
        return std::min(m_ResolveIndex + 1, total);
    if (m_Stage == Stage::RoundEnd)
        return total;
    return 0;
}

std::array<PlayerState, kPlayerCount> KnightsBoardController::GetShownPlayers(const PublicView& view) const
{
    std::array<PlayerState, kPlayerCount> players = view.players;
    if (view.phase != Phase::Resolution)
        return players;
    // 코어는 정산을 이미 반영했으므로, 아직 보여 주지 않은 이벤트만큼 되돌린다.
    const std::vector<RegionEvent>& events = m_Game.GetResolution();
    for (size_t i = static_cast<size_t>(GetShownEventCount()); i < events.size(); ++i)
    {
        for (int p = 0; p < kPlayerCount; ++p)
        {
            players[p].score -= events[i].scoreGained[p];
            players[p].knights -= events[i].knightsGained[p] - events[i].knightsLost[p];
        }
    }
    return players;
}

int KnightsBoardController::GetCurrentTurnPlayer() const
{
    switch (m_Stage)
    {
    case Stage::PlayerFlags:
    case Stage::PlayerKnights:
        return kHumanPlayer;
    case Stage::AiFlags:
    case Stage::AiKnights:
        return m_AiIndex;
    default:
        return -1;
    }
}

int KnightsBoardController::GetSelectedFlagCount() const
{
    return static_cast<int>(std::count(m_SelectedFlags.begin(), m_SelectedFlags.end(), true));
}

int KnightsBoardController::GetAllocatedKnights() const
{
    int total = 0;
    for (int count : m_Allocation)
        total += count;
    return total;
}

//***** 매 프레임 갱신

void KnightsBoardController::Refresh()
{
    const PublicView view = m_Game.GetView(kHumanPlayer);

    KnightsUi::SetTextIfChanged(*m_RoundText, "라운드 " + Num(view.round) + " / " + Num(kRoundCount));
    std::string stage;
    switch (m_Stage)
    {
    case Stage::PlayerFlags: stage = "깃발 단계 — 깃발 2개를 서로 다른 지역에 꽂으세요"; break;
    case Stage::AiFlags: stage = "깃발 단계 — AI가 깃발을 꽂는 중 (깃발은 모두 꽂은 뒤 한꺼번에 공개)"; break;
    case Stage::FlagReveal: stage = "깃발 공개!"; break;
    case Stage::PlayerKnights: stage = "기사 배치 — 깃발을 꽂은 몬스터 지역에 기사를 보내세요"; break;
    case Stage::AiKnights: stage = "기사 배치 — AI가 기사를 보내는 중 (배치는 모두 마친 뒤 한꺼번에 공개)"; break;
    case Stage::KnightReveal: stage = "기사 공개!"; break;
    case Stage::Resolving:
        stage = "정산 — 지역 " + Num(GetShownEventCount()) + " / " + Num(kRegionCount) + "   (화면을 누르면 다음으로)";
        break;
    case Stage::RoundEnd: stage = "라운드 " + Num(view.round) + " 종료"; break;
    case Stage::GameOver: stage = "게임 종료"; break;
    }
    KnightsUi::SetTextIfChanged(*m_StageText, stage);

    RefreshCards(view);
    RefreshScoreboard(view);
    RefreshActionPanel(view);
    RefreshGameOver();
}

void KnightsBoardController::RefreshCards(const PublicView& view)
{
    const auto& defs = GetRegionDefs();
    const int shownEvents = GetShownEventCount();
    const std::vector<RegionEvent>* events = view.phase == Phase::Resolution ? &m_Game.GetResolution() : nullptr;
    const int activeRegion = (m_Stage == Stage::Resolving && events && m_ResolveIndex < static_cast<int>(events->size()))
                                 ? (*events)[m_ResolveIndex].region
                                 : -1;
    const bool knightsVisible = m_Stage == Stage::KnightReveal || m_Stage == Stage::Resolving || m_Stage == Stage::RoundEnd;

    for (int r = 0; r < kRegionCount; ++r)
    {
        RegionCard& card = m_Cards[r];
        const bool monster = defs[r].kind == RegionKind::Monster;

        card.button->SetInteractable(m_Stage == Stage::PlayerFlags);

        //*** 강조: 깃발 고르는 중이면 고른 카드, 배치 중이면 내 깃발 몬스터 지역, 정산 중이면 지금 지역
        bool framed = false;
        UIColor frameColor = kSelectFrameColor;
        if (m_Stage == Stage::PlayerFlags)
            framed = m_SelectedFlags[r];
        else if (m_Stage == Stage::PlayerKnights)
            framed = monster && view.flags[kHumanPlayer][r];
        else if (r == activeRegion)
        {
            framed = true;
            frameColor = kResolveFrameColor;
        }
        card.frame->SetVisible(framed);
        card.frame->SetColor(frameColor);
        if (monster)
            card.button->SetColor(r == activeRegion ? kMonsterCardActiveColor : kMonsterCardColor);
        else
            card.button->SetColor(r == activeRegion ? kReinforceCardActiveColor : kReinforceCardColor);

        //*** 이 지역의 정산 이벤트(보여 준 것만)
        const RegionEvent* shown = nullptr;
        if (events)
        {
            for (int i = 0; i < shownEvents; ++i)
            {
                if ((*events)[i].region == r)
                    shown = &(*events)[i];
            }
        }

        //*** 큰 값
        std::string value;
        if (monster)
            value = Num(view.monsterScores[r]) + "점";
        else if (defs[r].kind == RegionKind::TrainingGround)
            value = "기사 +1";
        else
            value = (shown && shown->dice > 0) ? "주사위 " + Num(shown->dice) : "주사위";
        KnightsUi::SetTextIfChanged(*card.bigValue, value);

        //*** 깃발 칩: 고르는 중에는 내 선택만, 그 뒤로는 공개 뷰 그대로(공개 전엔 내 것만 들어 있다)
        for (int p = 0; p < kPlayerCount; ++p)
        {
            const bool flag = m_Stage == Stage::PlayerFlags ? (p == kHumanPlayer && m_SelectedFlags[r]) : view.flags[p][r];
            card.flagChips[p]->SetVisible(flag);
        }

        //*** 기사 칩: 배치 중에는 내 배치, 그 뒤로는 공개 뷰
        int totalKnights = 0;
        for (int p = 0; p < kPlayerCount; ++p)
        {
            int count = 0;
            if (m_Stage == Stage::PlayerKnights)
                count = p == kHumanPlayer ? m_Allocation[r] : 0;
            else if (m_Stage != Stage::PlayerFlags && m_Stage != Stage::GameOver)
                count = view.knights[p][r];
            totalKnights += count;
            card.knightChips[p]->SetVisible(count > 0);
            KnightsUi::SetTextIfChanged(*card.knightCounts[p], Num(count));
        }

        //*** 결과 줄
        std::string result;
        UIColor resultColor = KnightsUi::kSubTextColor;
        if (shown)
        {
            const EventText text = DescribeEvent(*shown);
            result = text.cardLine;
            switch (text.tone)
            {
            case EventTone::Win: resultColor = KnightsUi::kGoodColor; break;
            case EventTone::Wipe: resultColor = KnightsUi::kBadColor; break;
            case EventTone::Reinforce: resultColor = KnightsUi::kReinforceColor; break;
            case EventTone::Nothing: break;
            }
        }
        else if (knightsVisible && monster && totalKnights > 0)
        {
            result = "보낸 기사 합 " + Num(totalKnights) + "명 vs 몬스터 " + Num(view.monsterScores[r]) + "점";
        }
        if (m_Stage != Stage::GameOver)
        {
            KnightsUi::SetTextIfChanged(*card.result, result);
            card.result->SetColor(resultColor);
        }
    }
}

void KnightsBoardController::RefreshScoreboard(const PublicView& view)
{
    const std::array<PlayerState, kPlayerCount> players = GetShownPlayers(view);
    const int turn = GetCurrentTurnPlayer();
    const RegionEvent* current = nullptr;
    if (m_Stage == Stage::Resolving && view.phase == Phase::Resolution && m_ResolveIndex < static_cast<int>(m_Game.GetResolution().size()))
        current = &m_Game.GetResolution()[m_ResolveIndex];

    for (int p = 0; p < kPlayerCount; ++p)
    {
        ScoreRow& row = m_ScoreRows[p];
        row.background->SetColor(p == turn ? kScoreRowTurnColor : kScoreRowColor);
        KnightsUi::SetTextIfChanged(*row.score, Num(players[p].score) + "점");
        KnightsUi::SetTextIfChanged(*row.knights, Num(players[p].knights) + "명");

        //*** 상태: 정산 중이면 이번 지역의 변화, 아니면 제출 완료·차례
        std::string status;
        UIColor color = KnightsUi::kGoodColor;
        if (current)
        {
            if (current->scoreGained[p] > 0)
                status = "+" + Num(current->scoreGained[p]) + "점";
            else if (current->knightsLost[p] > 0)
            {
                status = "-" + Num(current->knightsLost[p]) + "명";
                color = KnightsUi::kBadColor;
            }
            else if (current->knightsGained[p] > 0)
            {
                status = "+" + Num(current->knightsGained[p]) + "명";
                color = KnightsUi::kReinforceColor;
            }
        }
        else if (view.submitted[p] && (m_Stage == Stage::AiFlags || m_Stage == Stage::AiKnights))
        {
            status = "완료";
        }
        else if (p == turn)
        {
            status = "차례";
            color = KnightsUi::kTitleColor;
        }
        KnightsUi::SetTextIfChanged(*row.status, status);
        row.status->SetColor(color);
    }
}

void KnightsBoardController::RefreshActionPanel(const PublicView& view)
{
    std::string message;
    std::string detail;
    switch (m_Stage)
    {
    case Stage::PlayerFlags:
        message = "깃발을 꽂을 지역 2곳을 고르세요 (" + Num(GetSelectedFlagCount()) + " / 2)";
        detail = "지역 카드를 눌러 선택 — 다시 누르면 해제. 증원 지역(훈련소, 용병 시장)도 고를 수 있습니다. 2곳을 고르면 [확정]을 누르세요.";
        break;
    case Stage::AiFlags:
        message = "AI " + Num(m_AiIndex) + " 차례 — 깃발을 고르는 중...";
        detail = "다른 플레이어의 깃발은 모두 꽂은 뒤 한꺼번에 공개됩니다.";
        break;
    case Stage::FlagReveal:
        message = "깃발 공개! 모두의 깃발이 드러났습니다.";
        detail = "다음: 깃발을 꽂은 몬스터 지역에 기사를 보냅니다.";
        break;
    case Stage::PlayerKnights:
        if (m_KnightRows[0].region < 0)
        {
            message = "보낼 지역이 없습니다 — [확정]을 누르세요";
            detail = "이번 라운드에는 몬스터 지역 깃발이 없습니다. 증원 지역에는 기사를 보내지 않고 깃발만으로 참여합니다.";
        }
        else
        {
            message = "깃발을 꽂은 몬스터 지역에 기사를 보내세요";
            detail = "보낸 기사 합이 몬스터 점수 이상이면 승리, 모자라면 그 지역의 기사는 모두 쓰러집니다. 0명도 됩니다.";
        }
        break;
    case Stage::AiKnights:
        message = "AI " + Num(m_AiIndex) + " 차례 — 기사를 보내는 중...";
        detail = "배치는 모두 마친 뒤 한꺼번에 공개됩니다.";
        break;
    case Stage::KnightReveal:
        message = "기사 공개! 지역 순서대로 정산합니다.";
        break;
    case Stage::Resolving:
        if (view.phase == Phase::Resolution && m_ResolveIndex < static_cast<int>(m_Game.GetResolution().size()))
        {
            const EventText text = DescribeEvent(m_Game.GetResolution()[m_ResolveIndex]);
            message = text.headline;
            detail = text.detail;
        }
        break;
    case Stage::RoundEnd:
        message = "라운드 " + Num(view.round) + " 종료";
        detail = view.round < kRoundCount ? "다음 라운드에는 몬스터 점수를 새로 뽑습니다." : "최종 결과를 확인합니다.";
        break;
    case Stage::GameOver:
        message = "게임 종료";
        break;
    }
    KnightsUi::SetTextIfChanged(*m_MessageText, message);
    KnightsUi::SetTextIfChanged(*m_DetailText, detail);

    //*** 기사 배치 줄과 남은 기사
    const bool placing = m_Stage == Stage::PlayerKnights;
    const int owned = view.players[kHumanPlayer].knights;
    const int remaining = owned - GetAllocatedKnights();
    for (KnightRow& row : m_KnightRows)
    {
        const bool visible = placing && row.region >= 0;
        row.root->SetVisible(visible);
        if (!visible)
            continue;
        const int count = m_Allocation[row.region];
        KnightsUi::SetTextIfChanged(*row.count, Num(count));
        SetButtonEnabled(*row.minus, count > 0);
        SetButtonEnabled(*row.plus, remaining > 0);
    }
    m_RemainingText->SetVisible(placing);
    KnightsUi::SetTextIfChanged(*m_RemainingText, "남은 기사 " + Num(remaining) + "명 / " + Num(owned) + "명");

    //*** [확정]: 깃발은 정확히 2개일 때만, 기사 배치는 항상(0명 허용)
    bool canConfirm = false;
    if (m_Stage == Stage::PlayerFlags)
        canConfirm = GetSelectedFlagCount() == kFlagsPerPlayer;
    else if (m_Stage == Stage::PlayerKnights)
        canConfirm = true;
    SetButtonEnabled(*m_ConfirmButton, canConfirm);
}

void KnightsBoardController::RefreshGameOver()
{
    const bool over = m_Stage == Stage::GameOver;
    m_GameOverRoot->SetVisible(over);
    if (!over)
        return;

    //*** 순위: 점수 내림차순, 같으면 남은 기사 내림차순
    std::array<int, kPlayerCount> order{};
    for (int p = 0; p < kPlayerCount; ++p)
        order[p] = p;
    auto better = [this](int a, int b)
    {
        const PlayerState& pa = m_Game.GetPlayer(a);
        const PlayerState& pb = m_Game.GetPlayer(b);
        if (pa.score != pb.score)
            return pa.score > pb.score;
        return pa.knights > pb.knights;
    };
    std::stable_sort(order.begin(), order.end(), better);

    const std::vector<int> winners = m_Game.GetWinners();
    std::string winnerText;
    if (winners.size() == 1)
    {
        winnerText = winners[0] == kHumanPlayer ? "나의 승리!" : PlayerName(winners[0]) + " 승리";
    }
    else
    {
        winnerText = "공동 우승: ";
        for (size_t i = 0; i < winners.size(); ++i)
        {
            if (i > 0)
                winnerText += ", ";
            winnerText += PlayerName(winners[i]);
        }
    }
    KnightsUi::SetTextIfChanged(*m_WinnerText, winnerText);

    for (int i = 0; i < kPlayerCount; ++i)
    {
        const int p = order[i];
        const PlayerState& state = m_Game.GetPlayer(p);
        int place = 1;
        for (int q = 0; q < kPlayerCount; ++q)
        {
            if (better(q, p))
                ++place;
        }
        const bool winner = std::find(winners.begin(), winners.end(), p) != winners.end();
        m_RankBackgrounds[i]->SetColor(winner ? kRankWinnerColor : kRankRowColor);
        if (auto* placeText = m_RankBackgrounds[i]->Find<UIText>("Place"))
            KnightsUi::SetTextIfChanged(*placeText, Num(place) + "위");
        m_RankChips[i]->SetColor(KnightsUi::kPlayerColors[p]);
        KnightsUi::SetTextIfChanged(*m_RankNames[i], PlayerName(p));
        KnightsUi::SetTextIfChanged(*m_RankScores[i], Num(state.score) + "점");
        KnightsUi::SetTextIfChanged(*m_RankKnights[i], "기사 " + Num(state.knights) + "명");
    }
}
