#pragma once

// 기사단 게임 화면들(타이틀, 보드)이 함께 쓰는 임시 2D UI 도우미. 아트가 들어오면 이 파일과 화면 코드만 바꾼다.

#include <Engine.h>

#include "KnightsGame/KnightsRules.h"
#include "Scripts/UIDemoTextures.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace KnightsUi
{
    //***** 공통 색
    constexpr Canape::UIColor kBackgroundColor{ 24, 28, 40, 255 };
    constexpr Canape::UIColor kTitleColor{ 255, 214, 102 };
    constexpr Canape::UIColor kPanelColor{ 36, 42, 58, 255 };
    constexpr Canape::UIColor kTextColor{ 236, 238, 244 };
    constexpr Canape::UIColor kSubTextColor{ 160, 168, 186 };
    constexpr Canape::UIColor kGoodColor{ 120, 220, 140 };
    constexpr Canape::UIColor kBadColor{ 255, 110, 110 };
    constexpr Canape::UIColor kReinforceColor{ 120, 190, 255 };
    constexpr Canape::UIColor kConfirmColor{ 64, 156, 92 };
    constexpr Canape::UIColor kQuitColor{ 140, 80, 90 };
    constexpr Canape::UIColor kNeutralButtonColor{ 78, 88, 116 };

    //***** 플레이어 고정 색: 나 금색, AI 1~4 빨강·파랑·초록·보라
    //*** 배열 크기를 적은 색 개수로 정한다(to_array). 하나라도 빠지면 아래 static_assert가 잡는다(빠진 칸이 흰색으로 채워지지 않게).
    constexpr auto kPlayerColors = std::to_array<Canape::UIColor>({
        { 250, 200, 70 },
        { 232, 84, 84 },
        { 80, 150, 245 },
        { 90, 200, 110 },
        { 180, 110, 235 },
    });
    static_assert(kPlayerColors.size() == Knights::kPlayerCount, "kPlayerColors needs exactly one color per player");

    //***** 둥근 사각형 텍스처. 화면 스크립트가 Start에서 만들고 OnDestroy에서 지운다.
    inline Canape::TextureResourceId CreateRoundedTexture()
    {
        const std::vector<uint8_t> rounded = UIDemoTextures::MakeRounded();
        return Canape::Application::Get().GetRenderer().CreateTexture(UIDemoTextures::kRoundedSize, UIDemoTextures::kRoundedSize,
            Canape::TextureFormat::RGBA8_UNORM, rounded);
    }

    inline void DestroyTexture(Canape::TextureResourceId& texture)
    {
        if (texture.IsValid())
        {
            Canape::Application::Get().GetRenderer().DestroyTexture(texture);
            texture = {};
        }
    }

    //*** 이미지에 둥근 텍스처를 9-slice로 입힌다. radius는 화면 모서리 반지름(기준 픽셀).
    inline void MakeRounded(Canape::UIImage& image, Canape::TextureResourceId texture, float radius)
    {
        using UIDemoTextures::kRoundedRadius;
        using UIDemoTextures::kRoundedSize;
        image.SetTextureResource(texture);
        image.SetNineSlice({ Canape::UIEdges(kRoundedRadius / kRoundedSize), Canape::UIEdges(radius) });
    }

    //***** 요소 만들기. 기본 앵커·피벗은 부모의 왼쪽 위이며, 필요하면 만든 뒤 바꾼다.
    inline Canape::UIImage* AddBackground(Canape::UIElement& root)
    {
        auto* background = root.AddChild<Canape::UIImage>("Background");
        background->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
        background->SetSize({ 0.0f, 0.0f });
        background->SetColor(kBackgroundColor);
        return background;
    }

    inline Canape::UIImage* AddPanel(Canape::UIElement& parent, std::string name, Canape::TextureResourceId texture,
                                     const Canape::Vector2& position, const Canape::Vector2& size, Canape::UIColor color,
                                     float radius = 14.0f)
    {
        auto* panel = parent.AddChild<Canape::UIImage>(std::move(name));
        panel->SetPosition(position);
        panel->SetSize(size);
        panel->SetColor(color);
        if (texture.IsValid())
        {
            MakeRounded(*panel, texture, radius);
        }
        return panel;
    }

    inline Canape::UIText* AddLabel(Canape::UIElement& parent, std::string name, std::string text, float fontSize,
                                    const Canape::Vector2& position, const Canape::Vector2& size,
                                    Canape::UIColor color = kTextColor,
                                    Canape::UITextAlign horizontal = Canape::UITextAlign::Start,
                                    Canape::UITextAlign vertical = Canape::UITextAlign::Center)
    {
        auto* label = parent.AddChild<Canape::UIText>(std::move(name));
        label->SetPosition(position);
        label->SetSize(size);
        label->SetText(std::move(text));
        label->SetFontSize(fontSize);
        label->SetColor(color);
        label->SetHorizontalAlign(horizontal);
        label->SetVerticalAlign(vertical);
        return label;
    }

    //*** 둥근 버튼 + 가운데 정렬 글자(자식 이름 "Label")
    inline Canape::UIButton* AddButton(Canape::UIElement& parent, std::string name, Canape::TextureResourceId texture,
                                       std::string text, float fontSize, const Canape::Vector2& position,
                                       const Canape::Vector2& size, Canape::UIColor color)
    {
        auto* button = parent.AddChild<Canape::UIButton>(std::move(name));
        button->SetPosition(position);
        button->SetSize(size);
        button->SetColor(color);
        MakeRounded(*button, texture, 14.0f);

        auto* label = button->AddChild<Canape::UIText>("Label");
        label->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
        label->SetSize({ 0.0f, 0.0f });
        label->SetText(std::move(text));
        label->SetFontSize(fontSize);
        label->SetColor(kTextColor);
        label->SetHorizontalAlign(Canape::UITextAlign::Center);
        label->SetVerticalAlign(Canape::UITextAlign::Center);
        return button;
    }

    //***** 매 프레임 갱신용: 값이 바뀐 때만 반영한다(글자 배치 캐시를 지키기 위해).
    inline void SetTextIfChanged(Canape::UIText& label, const std::string& text)
    {
        if (label.GetText() != text)
        {
            label.SetText(text);
        }
    }
}
