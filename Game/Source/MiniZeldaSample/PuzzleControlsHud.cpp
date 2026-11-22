#include "MiniZeldaSample/PuzzleControlsHud.h"

using namespace Canape;

namespace
{
    // 모든 퍼즐이 같은 패널을 쓴다(1920x1080 기준 크기).
    constexpr Vector2 kReferenceResolution{ 1920.0f, 1080.0f };
    constexpr Vector2 kPanelSize{ 560.0f, 300.0f };
    constexpr float kMargin = 32.0f;
    constexpr UIColor kPanelColor{ 96, 96, 102, 225 };
    constexpr UIColor kTextColor{ 255, 255, 255 };
    constexpr float kTitleSize = 28.0f;
    constexpr float kLineSize = 22.0f;
}

void PuzzleControlsHud::Create(const ScriptComponent& owner, std::initializer_list<std::pair<std::string, std::string>> lines,
    const std::string& titleText, float height)
{
    m_Canvas = owner.AddComponent<UICanvasComponent>(kReferenceResolution);
    if (!m_Canvas)
    {
        return;
    }
    UIElement& root = m_Canvas->GetRoot();

    auto* panel = root.AddChild<UIImage>("ControlsPanel");
    panel->SetAnchor({ 1.0f, 0.0f });
    panel->SetPivot({ 1.0f, 0.0f });
    panel->SetPosition({ -kMargin, kMargin });
    panel->SetSize({ kPanelSize.X, height > 0.0f ? height : kPanelSize.Y });
    panel->SetColor(kPanelColor);
    panel->SetClipChildren(true);
    panel->SetHitTestable(false); // 패널이 게임 쪽 마우스 입력을 가로채지 않게

    auto* box = panel->AddChild<UIVerticalBox>("ControlsBox");
    box->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    box->SetSize({ 0.0f, 0.0f });
    box->SetPadding(UIEdges(22.0f));
    box->SetSpacing(12.0f);
    box->SetChildAlign(UIBoxAlign::Stretch);
    box->SetHitTestable(false);

    auto* title = box->AddChild<UIText>("ControlsTitle");
    title->SetText(titleText);
    title->SetFontSize(kTitleSize);
    title->SetColor(kTextColor);
    title->SetHitTestable(false);

    int index = 0;
    for (const auto& [input, action] : lines)
    {
        auto* line = box->AddChild<UIText>("ControlsLine" + std::to_string(++index));
        line->SetText(input + " - " + action);
        line->SetFontSize(kLineSize);
        line->SetColor(kTextColor);
        line->SetWrap(true);
        line->SetHitTestable(false);
    }

    SetShown(false);
}

void PuzzleControlsHud::SetShown(bool shown)
{
    if (m_Canvas)
    {
        m_Canvas->SetEnabled(shown);
    }
}
