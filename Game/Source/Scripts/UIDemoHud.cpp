#include "Scripts/UIDemoHud.h"
#include "CNDebug/DebugLog.h"

#include <algorithm>

RTTR_REGISTRATION
{
    rttr::registration::class_<UIDemoHud>("UIDemoHud")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("drainPerSecond", &UIDemoHud::m_DrainPerSecond);
}

using namespace Canape;

namespace
{
    constexpr UIColor kPanelColor{ 18, 20, 26, 200 };
    constexpr UIColor kHealthColor{ 214, 64, 69 };
    constexpr UIColor kHealColor{ 64, 156, 92 };
    constexpr UIColor kPauseColor{ 70, 120, 210 };
    constexpr UIColor kResumeColor{ 220, 150, 50 };
}

void UIDemoHud::Start()
{
    auto* canvas = AddComponent<UICanvasComponent>(Vector2{ 1920.0f, 1080.0f });
    UIElement& root = canvas->GetRoot();

    // 체력바: 왼쪽 위 고정, 안쪽 채움은 테두리 4px을 남기고 늘어난다.
    auto* healthBack = root.AddChild<UIImage>("HealthBack");
    healthBack->SetAnchor({ 0.0f, 0.0f });
    healthBack->SetPivot({ 0.0f, 0.0f });
    healthBack->SetPosition({ 32.0f, 32.0f });
    healthBack->SetSize({ 420.0f, 36.0f });
    healthBack->SetColor(kPanelColor);

    m_HealthFill = healthBack->AddChild<UIImage>("HealthFill");
    m_HealthFill->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    m_HealthFill->SetPivot({ 0.5f, 0.5f });
    m_HealthFill->SetSize({ -8.0f, -8.0f });
    m_HealthFill->SetColor(kHealthColor);

    // 버튼: 오른쪽 위 고정
    auto* heal = root.AddChild<UIButton>("HealButton");
    heal->SetAnchor({ 1.0f, 0.0f });
    heal->SetPivot({ 1.0f, 0.0f });
    heal->SetPosition({ -32.0f, 32.0f });
    heal->SetSize({ 160.0f, 56.0f });
    heal->SetColor(kHealColor);
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
    m_PauseButton->OnClicked = [this]
    {
        m_Paused = !m_Paused;
        RefreshPauseButton();
        CN_LOG_GAME("UI", m_Paused ? "체력 감소 멈춤" : "체력 감소 재개");
    };
    RefreshPauseButton();
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
}

void UIDemoHud::RefreshPauseButton()
{
    if (m_PauseButton)
    {
        m_PauseButton->SetColor(m_Paused ? kResumeColor : kPauseColor);
    }
}
