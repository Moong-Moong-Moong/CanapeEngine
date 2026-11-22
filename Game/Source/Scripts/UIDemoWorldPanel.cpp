#include "Scripts/UIDemoWorldPanel.h"
#include "Scripts/UIDemoTextures.h"

#include <cmath>
#include <format>
#include <vector>

RTTR_REGISTRATION
{
    rttr::registration::class_<UIDemoWorldPanel>("UIDemoWorldPanel")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("worldUnitsPerPixel", &UIDemoWorldPanel::m_WorldUnitsPerPixel);
}

using namespace Canape;

namespace
{
    constexpr UIColor kPanelColor{ 18, 20, 26, 225 };
    constexpr UIColor kTitleColor{ 255, 214, 102 };
    constexpr UIColor kGaugeBackColor{ 255, 255, 255, 40 };
    constexpr UIColor kGaugeColor{ 80, 170, 240 };
    constexpr UIColor kNoteColor{ 170, 180, 195 };
}

void UIDemoWorldPanel::Start()
{
    const std::vector<uint8_t> rounded = UIDemoTextures::MakeRounded();
    m_RoundedTexture = Application::Get().GetRenderer().CreateTexture(
        UIDemoTextures::kRoundedSize, UIDemoTextures::kRoundedSize, TextureFormat::RGBA8_UNORM, rounded);
    const UIEdges sourceBorder(UIDemoTextures::kRoundedRadius / UIDemoTextures::kRoundedSize);

    m_Canvas = AddComponent<UICanvasComponent>();
    m_Canvas->SetMode(UICanvasMode::WorldSpace);
    m_Canvas->SetCanvasSize({ 480.0f, 260.0f });
    m_Canvas->SetWorldUnitsPerPixel(m_WorldUnitsPerPixel);
    UIElement& root = m_Canvas->GetRoot();

    auto* panel = root.AddChild<UIImage>("Panel");
    panel->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    panel->SetSize({ 0.0f, 0.0f });
    panel->SetColor(kPanelColor);
    panel->SetTextureResource(m_RoundedTexture);
    panel->SetNineSlice({ sourceBorder, UIEdges(20.0f) });

    auto* column = panel->AddChild<UIVerticalBox>("Column");
    column->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    column->SetSize({ 0.0f, 0.0f });
    column->SetPadding(UIEdges(24.0f));
    column->SetSpacing(12.0f);
    column->SetChildAlign(UIBoxAlign::Stretch);

    auto* title = column->AddChild<UIText>("Title");
    title->SetText("월드 패널");
    title->SetFontSize(40.0f);
    title->SetColor(kTitleColor);

    auto* body = column->AddChild<UIText>("Body");
    body->SetText("이 패널은 3D 공간에 서 있습니다. 앞에 있는 물체에 가려지고, "
        "다가가면 글자를 더 큰 배율로 다시 구워 선명하게 보입니다.");
    body->SetFontSize(24.0f);
    body->SetWrap(true);
    body->SetLineSpacing(1.1f);

    auto* gauge = column->AddChild<UIImage>("Gauge");
    gauge->SetSize({ 0.0f, 26.0f });
    gauge->SetColor(kGaugeBackColor);
    gauge->SetTextureResource(m_RoundedTexture);
    gauge->SetNineSlice({ sourceBorder, UIEdges(12.0f) });

    m_GaugeFill = gauge->AddChild<UIImage>("GaugeFill");
    m_GaugeFill->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    m_GaugeFill->SetSize({ 0.0f, 0.0f });
    m_GaugeFill->SetColor(kGaugeColor);
    m_GaugeFill->SetTextureResource(m_RoundedTexture);
    m_GaugeFill->SetNineSlice({ sourceBorder, UIEdges(12.0f) });

    m_RasterText = column->AddChild<UIText>("RasterInfo");
    m_RasterText->SetFontSize(20.0f);
    m_RasterText->SetColor(kNoteColor);
}

void UIDemoWorldPanel::Update()
{
    m_Time += Time::DeltaTime();
    if (m_GaugeFill)
    {
        m_GaugeFill->SetFillAmount(0.5f + 0.5f * std::sin(m_Time * 1.5f));
    }
    // 지난 프레임에 글자를 구운 배율(거리에 따라 1/4~4배)
    if (m_Canvas && m_RasterText && m_Canvas->GetRasterFactor() != m_ShownRaster)
    {
        m_ShownRaster = m_Canvas->GetRasterFactor();
        m_RasterText->SetText(std::format("글자 굽는 배율 ×{:g}", m_ShownRaster));
    }
}

void UIDemoWorldPanel::OnDestroy()
{
    if (m_RoundedTexture.IsValid())
    {
        Application::Get().GetRenderer().DestroyTexture(m_RoundedTexture);
        m_RoundedTexture = {};
    }
}
