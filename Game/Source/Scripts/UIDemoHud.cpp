#include "Scripts/UIDemoHud.h"
#include "Scripts/UIDemoTextures.h"
#include "CNDebug/DebugLog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <vector>

RTTR_REGISTRATION
{
    rttr::registration::class_<UIDemoHud>("UIDemoHud")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("drainPerSecond", &UIDemoHud::m_DrainPerSecond);
}

using namespace Canape;

namespace
{
    constexpr UIColor kPanelColor{ 18, 20, 26, 210 };
    constexpr UIColor kHealthColor{ 214, 64, 69 };
    constexpr UIColor kHealColor{ 64, 156, 92 };
    constexpr UIColor kPauseColor{ 70, 120, 210 };
    constexpr UIColor kResumeColor{ 220, 150, 50 };
    constexpr UIColor kTitleColor{ 255, 214, 102 };

    using UIDemoTextures::kRoundedRadius;
    using UIDemoTextures::kRoundedSize;
}

UIImage* UIDemoHud::AddRounded(UIElement& parent, std::string name, UIColor color, float border)
{
    auto* image = parent.AddChild<UIImage>(std::move(name));
    image->SetColor(color);
    image->SetTextureResource(m_RoundedTexture);
    const float source = kRoundedRadius;
    image->SetNineSlice({ UIEdges(source / kRoundedSize), UIEdges(border) });
    return image;
}

UIText* UIDemoHud::AddLabel(UIElement& parent, std::string name, std::string text, float size)
{
    auto* label = parent.AddChild<UIText>(std::move(name));
    label->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    label->SetSize({ 0.0f, 0.0f });
    label->SetText(std::move(text));
    label->SetFontSize(size);
    label->SetHorizontalAlign(UITextAlign::Center);
    label->SetVerticalAlign(UITextAlign::Center);
    return label;
}

void UIDemoHud::Start()
{
    const std::vector<uint8_t> rounded = UIDemoTextures::MakeRounded();
    m_RoundedTexture = Application::Get().GetRenderer().CreateTexture(kRoundedSize, kRoundedSize, TextureFormat::RGBA8_UNORM, rounded);

    auto* canvas = AddComponent<UICanvasComponent>(Vector2{ 1920.0f, 1080.0f });
    UIElement& root = canvas->GetRoot();

    //***** 체력바: 왼쪽 위 고정. 채움은 테두리 6px 안쪽에서 늘어나고, 글자는 가운데.
    UIImage* healthBack = AddRounded(root, "HealthBack", kPanelColor, 12.0f);
    healthBack->SetPosition({ 32.0f, 32.0f });
    healthBack->SetSize({ 420.0f, 44.0f });

    m_HealthFill = AddRounded(*healthBack, "HealthFill", kHealthColor, 10.0f);
    m_HealthFill->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    m_HealthFill->SetPivot({ 0.5f, 0.5f });
    m_HealthFill->SetSize({ -12.0f, -12.0f });

    m_HealthText = AddLabel(*healthBack, "HealthText", "HP 100%", 22.0f);

    //***** 버튼: 오른쪽 위 고정, 글자 붙음
    auto* heal = root.AddChild<UIButton>("HealButton");
    heal->SetAnchor({ 1.0f, 0.0f });
    heal->SetPivot({ 1.0f, 0.0f });
    heal->SetPosition({ -32.0f, 32.0f });
    heal->SetSize({ 160.0f, 56.0f });
    heal->SetColor(kHealColor);
    heal->SetTextureResource(m_RoundedTexture);
    heal->SetNineSlice({ UIEdges(kRoundedRadius / kRoundedSize), UIEdges(14.0f) });
    AddLabel(*heal, "HealLabel", "회복", 24.0f);
    heal->OnClicked = [this]
    {
        m_Health = 1.0f;
        CN_LOG_GAME("UI", "체력 회복");
    };

    m_PauseButton = root.AddChild<UIButton>("PauseButton");
    m_PauseButton->SetAnchor({ 1.0f, 0.0f });
    m_PauseButton->SetPivot({ 1.0f, 0.0f });
    m_PauseButton->SetPosition({ -208.0f, 32.0f });
    m_PauseButton->SetSize({ 160.0f, 56.0f });
    m_PauseButton->SetTextureResource(m_RoundedTexture);
    m_PauseButton->SetNineSlice({ UIEdges(kRoundedRadius / kRoundedSize), UIEdges(14.0f) });
    m_PauseLabel = AddLabel(*m_PauseButton, "PauseLabel", "일시정지", 24.0f);
    m_PauseButton->OnClicked = [this]
    {
        m_Paused = !m_Paused;
        RefreshPauseButton();
        CN_LOG_GAME("UI", m_Paused ? "체력 감소 멈춤" : "체력 감소 재개");
    };
    RefreshPauseButton();

    //***** 도움말: 왼쪽 아래 패널, 안쪽은 세로 박스. 패널 높이를 넘는 줄은 잘린다.
    UIImage* help = AddRounded(root, "HelpPanel", kPanelColor, 16.0f);
    help->SetAnchor({ 0.0f, 1.0f });
    help->SetPivot({ 0.0f, 1.0f });
    help->SetPosition({ 32.0f, -32.0f });
    help->SetSize({ 520.0f, 180.0f }); // 도움말 끝줄이 넘쳐 잘리는 것을 보여 주는 높이
    help->SetClipChildren(true);

    auto* helpBox = help->AddChild<UIVerticalBox>("HelpBox");
    helpBox->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    helpBox->SetSize({ 0.0f, 0.0f });
    helpBox->SetPadding(UIEdges(20.0f));
    helpBox->SetSpacing(10.0f);
    helpBox->SetChildAlign(UIBoxAlign::Stretch);

    auto* title = helpBox->AddChild<UIText>("HelpTitle");
    title->SetText("UI 2단계 데모");
    title->SetFontSize(26.0f);
    title->SetColor(kTitleColor);

    auto* body = helpBox->AddChild<UIText>("HelpBody");
    body->SetText("버튼을 누르면 체력이 회복되고, 일시정지 버튼은 체력 감소를 멈추거나 다시 시작합니다. "
        "이 문단은 패널 폭에 맞춰 어절 단위로 줄이 바뀌며, 패널 높이를 넘는 줄은 잘려서 보이지 않습니다. "
        "Long English sentences wrap at word boundaries as well.");
    body->SetFontSize(20.0f);
    body->SetWrap(true);
    body->SetLineSpacing(1.15f);

    //***** 슬롯: 아래 가운데, 가로 박스가 내용 크기에 맞춰진다.
    auto* slots = root.AddChild<UIHorizontalBox>("SlotBar");
    slots->SetAnchor({ 0.5f, 1.0f });
    slots->SetPivot({ 0.5f, 1.0f });
    slots->SetPosition({ 0.0f, -32.0f });
    slots->SetFitContent(true);
    slots->SetSpacing(12.0f);
    slots->SetChildAlign(UIBoxAlign::Center);
    constexpr std::array<UIColor, 5> slotColors{ { { 90, 110, 140 }, { 120, 90, 150 }, { 150, 110, 70 }, { 70, 130, 120 }, { 140, 80, 90 } } };
    for (size_t index = 0; index < slotColors.size(); ++index)
    {
        UIImage* slot = AddRounded(*slots, std::format("Slot{}", index + 1), slotColors.at(index), 12.0f);
        slot->SetSize({ 72.0f, 72.0f });
        AddLabel(*slot, std::format("SlotKey{}", index + 1), std::format("{}", index + 1), 28.0f);
    }
}

void UIDemoHud::Update()
{
    if (!m_Paused)
    {
        m_Health = std::max(m_Health - m_DrainPerSecond * Time::DeltaTime(), 0.0f);
    }
    if (m_HealthFill)
    {
        m_HealthFill->SetFillAmount(m_Health);
    }
    const int percent = static_cast<int>(std::lround(m_Health * 100.0f));
    if (m_HealthText && percent != m_ShownPercent)
    {
        m_ShownPercent = percent;
        m_HealthText->SetText(std::format("HP {}%", percent));
    }
}

void UIDemoHud::OnDestroy()
{
    if (m_RoundedTexture.IsValid())
    {
        Application::Get().GetRenderer().DestroyTexture(m_RoundedTexture);
        m_RoundedTexture = {};
    }
}

void UIDemoHud::RefreshPauseButton()
{
    if (m_PauseButton)
    {
        m_PauseButton->SetColor(m_Paused ? kResumeColor : kPauseColor);
    }
    if (m_PauseLabel)
    {
        m_PauseLabel->SetText(m_Paused ? "재개" : "일시정지");
    }
}
