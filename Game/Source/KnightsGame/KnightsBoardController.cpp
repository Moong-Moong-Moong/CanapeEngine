#include "KnightsGame/KnightsBoardController.h"
#include "KnightsGame/KnightsBoardText.h"
#include "KnightsGame/KnightsUi.h"
#include "CNDebug/DebugLog.h"

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
    // 카드 결과 줄 상자. 글이 길면 아래 글자 크기 순서대로 줄여 상자에 맞추고, 그래도 넘치면 상자가 잘라 낸다.
    constexpr float kResultX = 20.0f;
    constexpr float kResultY = 210.0f;
    constexpr float kResultWidth = 410.0f;
    constexpr float kResultHeight = 80.0f;
    constexpr std::array<float, 4> kResultFontSizes{ 22.0f, 20.0f, 18.0f, 16.0f };
    // 진행 기록 창
    constexpr float kLogWidth = 1500.0f;
    constexpr float kLogHeight = 940.0f;
    constexpr float kLogPadding = 40.0f;
    constexpr float kLogContentTop = 132.0f;
    constexpr float kLogViewportHeight = kLogHeight - kLogContentTop - 32.0f;
    constexpr float kLogWheelStep = 90.0f;  // 휠 한 칸에 움직이는 양

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
    constexpr uint8_t kPreviousResultAlpha = 130;  // 지난 라운드 결과 줄은 흐리게

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
    BuildLogPanel(root);  // 종료 화면 위에서도 열 수 있도록 마지막에 만든다.

    NewGame();
    Refresh();

    Application::Get().GetInputManager().SetCursorRequested(true);
}

void KnightsBoardController::Update()
{
    // 씬 언로드가 지연되어 이전 씬의 OnDestroy(false)가 늦게 올 수 있으므로 매 프레임 다시 요청한다.
    Application::Get().GetInputManager().SetCursorRequested(true);

    const bool clicked = ConsumeRawClick();
    // 기록 창이 열려 있으면 진행을 멈춘다(시간도 흐르지 않고, 창 안의 클릭이 정산을 넘기지 않는다).
    if (!m_LogOpen)
    {
        m_Timer += Time::DeltaTime();
        AdvanceStage(clicked);
    }
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
    m_StageText = AddLabel(*bar, "Stage", "", 32.0f, { 330.0f, 0.0f }, { 1300.0f, kTopBarHeight });

    auto* log = KnightsUi::AddButton(*bar, "Log", m_RoundedTexture, "기록", 30.0f, { -20.0f, 0.0f }, { 180.0f, 64.0f },
                                     KnightsUi::kNeutralButtonColor);
    log->SetAnchor({ 1.0f, 0.5f });
    log->SetPivot({ 1.0f, 0.5f });
    log->OnClicked = [this] { SetLogOpen(true); };
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

        std::string sub;  // 몬스터 범위는 RefreshCards가 라운드에 맞춰 채운다.
        if (def.kind == RegionKind::TrainingGround)
            sub = "증원 · 깃발 1개당 기사 +1";
        else if (def.kind == RegionKind::MercenaryMarket)
            sub = "증원 · 주사위 → 깃발 수로 나눔";
        card.sub = AddLabel(face, "Sub", sub, 22.0f, { 20.0f, 64.0f }, { 410.0f, 32.0f }, KnightsUi::kSubTextColor);

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

        //*** 결과 줄(정산 뒤 다음 라운드 기사 공개 전까지 남는다). 잘라 내는 상자 안에 두어 카드 밖으로 넘치지 않게 한다.
        auto* resultBox = face.AddChild<UIElement>("ResultBox");
        resultBox->SetPosition({ kResultX, kResultY });
        resultBox->SetSize({ kResultWidth, kResultHeight });
        resultBox->SetClipChildren(true);
        card.result = AddLabel(*resultBox, "Result", "", kResultFontSizes[0], { 0.0f, 0.0f }, { kResultWidth, kResultHeight },
                               KnightsUi::kSubTextColor, UITextAlign::Start, UITextAlign::Start);
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

    //*** 진행 오류로 멈췄을 때 빠져나갈 길: 기사 배치 줄 자리(왼쪽 아래)에 [다시 하기] [타이틀로]
    m_StallButtons = panel->AddChild<UIElement>("StallButtons");
    m_StallButtons->SetPosition({ 28.0f, kActionHeight - 28.0f - 90.0f });
    m_StallButtons->SetSize({ 580.0f, 90.0f });
    auto* again = KnightsUi::AddButton(*m_StallButtons, "Again", m_RoundedTexture, "다시 하기", 36.0f, { 0.0f, 0.0f },
                                       { 280.0f, 90.0f }, KnightsUi::kConfirmColor);
    again->OnClicked = [this] { NewGame(); };
    auto* title = KnightsUi::AddButton(*m_StallButtons, "ToTitle", m_RoundedTexture, "타이틀로", 36.0f, { 300.0f, 0.0f },
                                       { 280.0f, 90.0f }, KnightsUi::kNeutralButtonColor);
    title->OnClicked = [this] { GoToTitle(); };
    m_StallButtons->SetVisible(false);
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
        m_RankPlaces[i] = AddLabel(row, "Place", "", 30.0f, { 20.0f, 0.0f }, { 70.0f, 68.0f }, KnightsUi::kSubTextColor);
        m_RankChips[i] = KnightsUi::AddPanel(row, "Color", m_RoundedTexture, { 96.0f, 18.0f }, { 32.0f, 32.0f }, KnightsUi::kPlayerColors[i], 8.0f);
        m_RankNames[i] = AddLabel(row, "Name", "", 34.0f, { 146.0f, 0.0f }, { 220.0f, 68.0f });
        m_RankScores[i] = AddLabel(row, "Score", "", 34.0f, { 360.0f, 0.0f }, { 160.0f, 68.0f }, KnightsUi::kTitleColor, UITextAlign::End);
        m_RankKnights[i] = AddLabel(row, "Knights", "", 30.0f, { 530.0f, 0.0f }, { 170.0f, 68.0f }, KnightsUi::kTextColor, UITextAlign::End);
    }

    constexpr float buttonWidth = 240.0f;
    constexpr float buttonGap = 30.0f;
    constexpr float buttonLeft = (width - 3.0f * buttonWidth - 2.0f * buttonGap) * 0.5f;
    auto* again = KnightsUi::AddButton(*panel, "Again", m_RoundedTexture, "다시 하기", 36.0f, { buttonLeft, height - 120.0f },
                                       { buttonWidth, 90.0f }, KnightsUi::kConfirmColor);
    again->OnClicked = [this] { NewGame(); };

    auto* log = KnightsUi::AddButton(*panel, "Log", m_RoundedTexture, "기록 보기", 36.0f,
                                     { buttonLeft + buttonWidth + buttonGap, height - 120.0f }, { buttonWidth, 90.0f },
                                     KnightsUi::kNeutralButtonColor);
    log->OnClicked = [this] { SetLogOpen(true); };

    auto* title = KnightsUi::AddButton(*panel, "ToTitle", m_RoundedTexture, "타이틀로", 36.0f,
                                       { buttonLeft + 2.0f * (buttonWidth + buttonGap), height - 120.0f }, { buttonWidth, 90.0f },
                                       KnightsUi::kNeutralButtonColor);
    title->OnClicked = [this] { GoToTitle(); };

    overlay->SetVisible(false);
}

void KnightsBoardController::BuildLogPanel(UIElement& root)
{
    //*** 화면 전체를 어둡게 덮어 아래 보드·종료 화면 입력을 막는다.
    auto* overlay = root.AddChild<UIImage>("Log");
    overlay->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    overlay->SetSize({ 0.0f, 0.0f });
    overlay->SetColor(kOverlayColor);
    overlay->SetHitTestable(true);
    m_LogRoot = overlay;

    auto* panel = KnightsUi::AddPanel(*overlay, "Panel", m_RoundedTexture, { 0.0f, 0.0f }, { kLogWidth, kLogHeight },
                                      KnightsUi::kPanelColor, 20.0f);
    panel->SetAnchor({ 0.5f, 0.5f });
    panel->SetPivot({ 0.5f, 0.5f });

    AddLabel(*panel, "Title", "진행 기록", 44.0f, { 40.0f, 20.0f }, { 400.0f, 64.0f }, KnightsUi::kTitleColor);
    AddLabel(*panel, "Hint", "최근 라운드가 위 · 마우스 휠로 스크롤 · 보는 동안 게임은 멈춥니다", 22.0f, { 40.0f, 84.0f },
             { 1000.0f, 32.0f }, KnightsUi::kSubTextColor);

    auto* close = KnightsUi::AddButton(*panel, "Close", m_RoundedTexture, "닫기", 30.0f, { -32.0f, 24.0f }, { 180.0f, 64.0f },
                                       KnightsUi::kNeutralButtonColor);
    close->SetAnchor({ 1.0f, 0.0f });
    close->SetPivot({ 1.0f, 0.0f });
    close->OnClicked = [this] { SetLogOpen(false); };

    //*** 내용 상자: 넘치는 부분은 잘라 내고, 안쪽 세로 박스를 위아래로 움직여 스크롤한다.
    auto* viewport = panel->AddChild<UIElement>("Viewport");
    viewport->SetPosition({ kLogPadding, kLogContentTop });
    viewport->SetSize({ kLogWidth - 2.0f * kLogPadding, kLogViewportHeight });
    viewport->SetClipChildren(true);

    auto* content = viewport->AddChild<UIVerticalBox>("Content");
    content->SetSize({ kLogWidth - 2.0f * kLogPadding, kLogViewportHeight });
    content->SetChildAlign(UIBoxAlign::Stretch);
    content->SetSpacing(6.0f);
    m_LogContent = content;

    m_LogEmpty = AddLabel(*content, "Empty", "아직 끝난 라운드가 없습니다.", 28.0f, {}, { 0.0f, 60.0f }, KnightsUi::kSubTextColor);
    for (int i = 0; i < kRoundCount; ++i)
    {
        // 라운드 사이 여백. 맨 위 라운드 앞에서는 RefreshLog가 숨긴다.
        m_LogSpacers[i] = content->AddChild<UIElement>("RoundGap" + Num(i));
        m_LogSpacers[i]->SetSize({ 0.0f, 22.0f });
        m_LogTitles[i] = AddLabel(*content, "RoundTitle" + Num(i), "", 32.0f, {}, { 0.0f, 56.0f }, KnightsUi::kTitleColor,
                                  UITextAlign::Start, UITextAlign::End);
        m_LogBodies[i] = AddLabel(*content, "RoundBody" + Num(i), "", 24.0f, {}, { 0.0f, 0.0f }, KnightsUi::kTextColor,
                                  UITextAlign::Start, UITextAlign::Start);
        m_LogBodies[i]->SetWrap(true);
    }

    overlay->SetVisible(false);
}

void KnightsBoardController::GoToTitle()
{
    SceneManager& sceneManager = Application::Get().GetSceneManager();
    Scene* currentScene = GetOwnerObject()->GetScene();
    if (sceneManager.OpenScene("KnightsTitle"))
    {
        sceneManager.UnloadScene(currentScene);
    }
    else
    {
        // 실패하면 지금 씬에 그대로 남는다. 조용히 넘어가지 않도록 로그와 화면에 남긴다.
        ReportProblem("타이틀 씬(KnightsTitle)을 열지 못했습니다");
    }
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
    m_Stalled = false;
    m_LastEvents.clear();
    SetLogOpen(false);
    m_LogShownCount = -1;
    // 카드 결과 줄·칩·점수판은 매 프레임 Refresh가 새 상태로 다시 그린다. 종료 화면은 EnterStage가 숨긴다.
    EnterStage(Stage::PlayerFlags);
}

void KnightsBoardController::EnterStage(Stage stage)
{
    m_Stage = stage;
    m_Timer = 0.0f;
    if (!m_Stalled)
        m_ProblemText.clear();
    m_GameOverRoot->SetVisible(stage == Stage::GameOver);
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
    case Stage::GameOver:
        ShowGameOver();
        break;
    default:
        break;
    }
}

void KnightsBoardController::AdvanceStage(bool skipRequested)
{
    // 복구할 수 없는 오류가 나면 화면에 알린 채 멈춘다(잘못된 단계로 넘어가 조용히 굳는 것보다 낫다).
    if (m_Stalled)
        return;

    switch (m_Stage)
    {
    case Stage::AiFlags:
        if (m_AiIndex < kPlayerCount && m_Timer >= kAiTurnDelay)
        {
            m_Timer = 0.0f;
            if (SubmitAiFlags(m_AiIndex))
                ++m_AiIndex;
        }
        // 마지막 AI가 제출한 같은 프레임에 넘어간다("AI 5 차례"가 한 프레임 보이지 않게).
        if (m_AiIndex >= kPlayerCount && RequireCorePhase(Phase::KnightPlacement, "AI 깃발 제출 뒤"))
            EnterStage(Stage::FlagReveal);
        break;
    case Stage::FlagReveal:
        if (m_Timer >= kRevealHold && RequireCorePhase(Phase::KnightPlacement, "깃발 공개 뒤"))
            EnterStage(Stage::PlayerKnights);
        break;
    case Stage::AiKnights:
        if (m_AiIndex < kPlayerCount && m_Timer >= kAiTurnDelay)
        {
            m_Timer = 0.0f;
            if (SubmitAiKnights(m_AiIndex))
                ++m_AiIndex;
        }
        if (m_AiIndex >= kPlayerCount && RequireCorePhase(Phase::Resolution, "AI 기사 제출 뒤"))
            EnterStage(Stage::KnightReveal);
        break;
    case Stage::KnightReveal:
        if (m_Timer >= kRevealHold && RequireCorePhase(Phase::Resolution, "기사 공개 뒤"))
            EnterStage(Stage::Resolving);
        break;
    case Stage::Resolving:
        if (!RequireCorePhase(Phase::Resolution, "정산 중"))
            break;
        if (m_Timer >= kResolveStep || skipRequested)
        {
            m_Timer = 0.0f;
            if (++m_ResolveIndex >= static_cast<int>(m_Game.GetResolution().size()))
                EnterStage(Stage::RoundEnd);
        }
        break;
    case Stage::RoundEnd:
        if ((m_Timer >= kRoundEndHold || skipRequested) && RequireCorePhase(Phase::Resolution, "라운드 종료"))
        {
            // 코어는 AdvanceRound에서 정산 기록을 비우므로, 다음 라운드에 보여 줄 사본을 먼저 남긴다.
            m_LastEvents = m_Game.GetResolution();
            m_Game.AdvanceRound();
            if (m_Game.GetPhase() == Phase::GameOver)
                EnterStage(Stage::GameOver);
            else if (RequireCorePhase(Phase::FlagPlacement, "다음 라운드 시작"))
                EnterStage(Stage::PlayerFlags);
        }
        break;
    default:
        break;
    }
}

bool KnightsBoardController::SubmitAiFlags(int ai)
{
    const SubmitError error = m_Game.SubmitFlags(ai, m_Ais[ai]->ChooseFlags(m_Game.GetView(ai)));
    if (error == SubmitError::None)
        return true;
    ReportProblem(PlayerName(ai) + " 깃발 제출 거부: " + SubmitErrorName(error) + " (증원 지역으로 대신 제출)");

    // AI는 항상 유효한 선택을 돌려주지만, 혹시 거부되면 증원 두 곳으로 대신해 진행이 멈추지 않게 한다.
    const SubmitError fallback = m_Game.SubmitFlags(ai, GetFallbackFlags());
    if (fallback == SubmitError::None)
        return true;
    Stall(PlayerName(ai) + " 대체 깃발 제출도 거부: " + SubmitErrorName(fallback));
    return false;
}

bool KnightsBoardController::SubmitAiKnights(int ai)
{
    const SubmitError error = m_Game.SubmitKnights(ai, m_Ais[ai]->ChooseKnights(m_Game.GetView(ai)));
    if (error == SubmitError::None)
        return true;
    ReportProblem(PlayerName(ai) + " 기사 배치 거부: " + SubmitErrorName(error) + " (0명 배치로 대신 제출)");

    // 0명 배치는 기사 배치 단계에서 항상 유효하다.
    const SubmitError fallback = m_Game.SubmitKnights(ai, KnightAllocation{});
    if (fallback == SubmitError::None)
        return true;
    Stall(PlayerName(ai) + " 대체 기사 배치도 거부: " + SubmitErrorName(fallback));
    return false;
}

bool KnightsBoardController::RequireCorePhase(Phase expected, const char* where)
{
    if (m_Game.GetPhase() == expected)
        return true;
    Stall(std::string(where) + ": 룰 코어 단계가 예상과 다름(" + Num(static_cast<int>(m_Game.GetPhase())) + ", 기대 " +
          Num(static_cast<int>(expected)) + ")");
    return false;
}

void KnightsBoardController::ReportProblem(const std::string& text)
{
    CN_LOG_GAME("Knights", text);
    m_ProblemText = text;
}

void KnightsBoardController::Stall(const std::string& text)
{
    ReportProblem(text);
    m_Stalled = true;
}

FlagChoice KnightsBoardController::GetFallbackFlags()
{
    // 몬스터가 아닌 지역을 앞에서부터 고르고, 모자라면 남은 지역으로 채운다(항상 서로 다른 2곳).
    const auto& defs = GetRegionDefs();
    FlagChoice choice{};
    int count = 0;
    for (int r = 0; r < kRegionCount && count < kFlagsPerPlayer; ++r)
    {
        if (defs[r].kind != RegionKind::Monster)
            choice[count++] = r;
    }
    for (int r = 0; r < kRegionCount && count < kFlagsPerPlayer; ++r)
    {
        if (defs[r].kind == RegionKind::Monster)
            choice[count++] = r;
    }
    return choice;
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
        // 코어가 받아들인 경우에만 넘어간다. 거부되면 이유를 로그와 화면에 남기고 이 단계에 머문다.
        const SubmitError error = m_Game.SubmitFlags(kHumanPlayer, choice);
        if (error == SubmitError::None)
            EnterStage(Stage::AiFlags);
        else
            ReportProblem("나 깃발 제출 거부: " + std::string(SubmitErrorName(error)));
    }
    else if (m_Stage == Stage::PlayerKnights)
    {
        // 0명 배치도 그대로 제출한다(깃발이 증원 지역에만 있거나 기사가 없어도 막히지 않음).
        const SubmitError error = m_Game.SubmitKnights(kHumanPlayer, m_Allocation);
        if (error == SubmitError::None)
            EnterStage(Stage::AiKnights);
        else
            ReportProblem("나 기사 배치 거부: " + std::string(SubmitErrorName(error)));
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
        return m_AiIndex < kPlayerCount ? m_AiIndex : -1;
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
    RefreshLog();
}

//***** 진행 기록 창

void KnightsBoardController::SetLogOpen(bool open)
{
    m_LogOpen = open;
    m_LogScroll = 0.0f;  // 열 때마다 가장 최근 라운드부터
    m_LogRoot->SetVisible(open);
}

int KnightsBoardController::GetVisibleHistoryCount() const
{
    const int count = static_cast<int>(m_Game.GetHistory().size());
    // 코어는 기사 공개 순간 이번 라운드를 기록에 넣지만, 화면은 정산을 지역별로 천천히 보여 준다.
    // 그 연출이 끝나(RoundEnd) 전에는 이번 라운드를 숨긴다.
    if (m_Game.GetPhase() == Phase::Resolution && m_Stage != Stage::RoundEnd)
        return std::max(0, count - 1);
    return count;
}

void KnightsBoardController::RefreshLog()
{
    if (!m_LogOpen)
        return;

    //*** 기록 수가 바뀐 때만 글을 다시 채운다(0번 칸 = 가장 최근 라운드).
    const int count = GetVisibleHistoryCount();
    if (count != m_LogShownCount)
    {
        m_LogShownCount = count;
        const auto& history = m_Game.GetHistory();
        m_LogEmpty->SetVisible(count == 0);
        for (int i = 0; i < kRoundCount; ++i)
        {
            const bool used = i < count;
            m_LogSpacers[i]->SetVisible(used && i > 0);
            m_LogTitles[i]->SetVisible(used);
            m_LogBodies[i]->SetVisible(used);
            if (!used)
                continue;
            const RoundLogText text = DescribeRound(history[count - 1 - i]);
            KnightsUi::SetTextIfChanged(*m_LogTitles[i], text.title);
            KnightsUi::SetTextIfChanged(*m_LogBodies[i], text.body);
        }
    }

    //*** 휠 스크롤. UI가 포인터를 잡은 프레임에는 GetMouseWheel이 0이므로 원본 입력을 읽는다.
    const float wheel = Application::Get().GetInputManager().GetInput().MouseWheel;
    m_LogScroll -= wheel * kLogWheelStep;

    // 내용 높이는 지난 프레임 배치 결과로 잰다(마지막으로 보이는 자식의 아래 끝).
    float contentHeight = 0.0f;
    const float scale = m_LogContent->GetLayoutScale();
    if (scale > 0.0f)
    {
        const float top = m_LogContent->GetLayoutRect().Y;
        for (const auto& child : m_LogContent->GetChildren())
        {
            if (child->IsVisible())
            {
                const UIRect& rect = child->GetLayoutRect();
                contentHeight = std::max(contentHeight, (rect.Y + rect.Height - top) / scale);
            }
        }
    }
    const float maxScroll = std::max(0.0f, contentHeight - kLogViewportHeight);
    m_LogScroll = std::clamp(m_LogScroll, 0.0f, maxScroll);
    m_LogContent->SetPosition({ 0.0f, -m_LogScroll });
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
        {
            const ScoreRange range = GetMonsterRange(r, view.round);
            KnightsUi::SetTextIfChanged(*card.sub, "몬스터 · 점수 범위 " + Num(range.min) + "~" + Num(range.max));
            value = Num(view.monsterScores[r]) + "점";
        }
        else if (defs[r].kind == RegionKind::TrainingGround)
            value = "기사 +1";
        else
            value = "주사위 " + Num(view.marketDice);  // 라운드 시작에 공개된 값
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

        //*** 결과 줄: 이번 라운드 결과, 없으면(기사 공개 전) 지난 라운드 결과를 흐리게
        const RegionEvent* previous = nullptr;
        if (!shown && !knightsVisible)
        {
            for (const RegionEvent& event : m_LastEvents)
            {
                if (event.region == r)
                    previous = &event;
            }
        }
        std::string result;
        UIColor resultColor = KnightsUi::kSubTextColor;
        if (shown || previous)
        {
            const EventText text = DescribeEvent(shown ? *shown : *previous);
            result = text.cardLine;
            switch (text.tone)
            {
            case EventTone::Win: resultColor = KnightsUi::kGoodColor; break;
            case EventTone::Wipe: resultColor = KnightsUi::kBadColor; break;
            case EventTone::Reinforce: resultColor = KnightsUi::kReinforceColor; break;
            case EventTone::Nothing: break;
            }
            if (previous)
            {
                result = "지난 라운드: " + result;
                resultColor.A = kPreviousResultAlpha;
            }
        }
        else if (knightsVisible && monster && totalKnights > 0)
        {
            result = "보낸 기사 합 " + Num(totalKnights) + "명 vs 몬스터 " + Num(view.monsterScores[r]) + "점";
        }
        if (m_Stage != Stage::GameOver)
            SetCardResult(card, result, resultColor);
    }
}

void KnightsBoardController::SetCardResult(RegionCard& card, const std::string& text, UIColor color)
{
    UIText& label = *card.result;
    label.SetColor(color);
    // 글자나 화면 배율이 바뀐 때만 다시 맞춘다(글자 배치 캐시를 지키기 위해).
    const float scale = label.GetLayoutScale() > 0.0f ? label.GetLayoutScale() : 1.0f;
    if (label.GetText() == text && card.resultFitScale == scale)
        return;
    label.SetText(text);
    card.resultFitScale = scale;
    // 큰 글자부터 실제 화면 배율로 줄바꿈 높이를 재서, 상자 높이에 들어가는 첫 크기를 쓴다.
    // 가장 작은 크기로도 넘치면 상자(ResultBox)가 잘라 내고, 전체 문구는 정산 때 하단 패널에 나온다.
    for (float size : kResultFontSizes)
    {
        label.SetFontSize(size);
        if (label.GetPreferredHeight(kResultWidth * scale, scale) <= kResultHeight * scale)
            break;
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
        // 마지막 AI가 제출하면 같은 프레임에 다음 단계로 넘어가므로, 범위 밖 번호("AI 5")는 그리지 않는다.
        message = m_AiIndex < kPlayerCount ? PlayerName(m_AiIndex) + " 차례 — 깃발을 고르는 중..." : "AI가 깃발을 모두 꽂았습니다";
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
        message = m_AiIndex < kPlayerCount ? PlayerName(m_AiIndex) + " 차례 — 기사를 보내는 중..." : "AI가 기사를 모두 보냈습니다";
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
        if (view.round >= kRoundCount)
            detail = "최종 결과를 확인합니다.";
        else if (view.round + 1 == kLateRoundStart)
            detail = "다음 라운드부터 몬스터 점수 범위가 " + Num(kLateRoundMonsterBonus) + "씩 오릅니다.";
        else
            detail = "다음 라운드에는 몬스터 점수를 새로 뽑습니다.";
        break;
    case Stage::GameOver:
        message = "게임 종료";
        break;
    }
    //*** 제출 거부·진행 오류가 있으면 보조 문구 자리에 빨간색으로 보여 준다(로그에도 남아 있다).
    //*** 멈췄으면 [다시 하기] [타이틀로]로 빠져나갈 수 있게 한다.
    if (m_Stalled)
        message = "진행 오류로 멈췄습니다 — [다시 하기] 또는 [타이틀로]";
    m_StallButtons->SetVisible(m_Stalled);
    if (!m_ProblemText.empty())
        detail = "문제: " + m_ProblemText;
    m_DetailText->SetColor(m_ProblemText.empty() ? KnightsUi::kSubTextColor : KnightsUi::kBadColor);
    KnightsUi::SetTextIfChanged(*m_MessageText, message);
    KnightsUi::SetTextIfChanged(*m_DetailText, detail);

    //*** 기사 배치 줄과 남은 기사
    const bool placing = m_Stage == Stage::PlayerKnights && !m_Stalled;  // 멈춤 버튼과 같은 자리이므로 멈추면 숨긴다
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
    if (m_Stalled)
        canConfirm = false;
    else if (m_Stage == Stage::PlayerFlags)
        canConfirm = GetSelectedFlagCount() == kFlagsPerPlayer;
    else if (m_Stage == Stage::PlayerKnights)
        canConfirm = true;
    SetButtonEnabled(*m_ConfirmButton, canConfirm);
}

void KnightsBoardController::ShowGameOver()
{
    //*** 순위: 점수 내림차순, 같으면 남은 기사 내림차순. GameOver에 들어갈 때 한 번만 채운다.
    std::array<PlayerState, kPlayerCount> players{};
    std::array<int, kPlayerCount> order{};
    for (int p = 0; p < kPlayerCount; ++p)
    {
        players[p] = m_Game.GetPlayer(p);
        order[p] = p;
    }
    auto better = [&players](int a, int b)
    {
        if (players[a].score != players[b].score)
            return players[a].score > players[b].score;
        return players[a].knights > players[b].knights;
    };
    std::stable_sort(order.begin(), order.end(), better);

    const std::vector<int> winners = m_Game.GetWinners();
    std::string winnerText;
    if (winners.empty())
    {
        // GetWinners는 GameOver 단계에서만 채워진다. 비어 있으면 이름 없는 "공동 우승"을 띄우지 않는다.
        ReportProblem("종료 화면: 승자 목록이 비어 있음");
        winnerText = "게임 종료";
    }
    else if (winners.size() == 1)
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
    m_WinnerText->SetText(winnerText);

    for (int i = 0; i < kPlayerCount; ++i)
    {
        const int p = order[i];
        int place = 1;
        for (int q = 0; q < kPlayerCount; ++q)
        {
            if (better(q, p))
                ++place;
        }
        const bool winner = std::find(winners.begin(), winners.end(), p) != winners.end();
        m_RankBackgrounds[i]->SetColor(winner ? kRankWinnerColor : kRankRowColor);
        m_RankPlaces[i]->SetText(Num(place) + "위");
        m_RankChips[i]->SetColor(KnightsUi::kPlayerColors[p]);
        m_RankNames[i]->SetText(PlayerName(p));
        m_RankScores[i]->SetText(Num(players[p].score) + "점");
        m_RankKnights[i]->SetText("기사 " + Num(players[p].knights) + "명");
    }
}
