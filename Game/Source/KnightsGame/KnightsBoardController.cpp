#include "KnightsGame/KnightsBoardController.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<KnightsBoardController>("KnightsBoardController")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

using namespace Canape;

namespace
{
    constexpr UIColor kBackgroundColor{ 24, 28, 40, 255 };
    constexpr UIColor kTitleColor{ 255, 214, 102 };
}

void KnightsBoardController::Start()
{
    auto* canvas = AddComponent<UICanvasComponent>(Vector2{ 1920.0f, 1080.0f });
    UIElement& root = canvas->GetRoot();

    auto* background = root.AddChild<UIImage>("Background");
    background->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    background->SetSize({ 0.0f, 0.0f });
    background->SetColor(kBackgroundColor);

    auto* title = root.AddChild<UIText>("Title");
    title->SetAnchor({ 0.5f, 0.5f });
    title->SetPivot({ 0.5f, 0.5f });
    title->SetSize({ 1200.0f, 140.0f });
    title->SetText("기사단 게임");
    title->SetFontSize(96.0f);
    title->SetColor(kTitleColor);
    title->SetHorizontalAlign(UITextAlign::Center);
    title->SetVerticalAlign(UITextAlign::Center);

    Application::Get().GetInputManager().SetCursorRequested(true);
}

void KnightsBoardController::Update()
{
    // 씬 언로드가 지연되어 이전 씬의 OnDestroy(false)가 늦게 올 수 있으므로 매 프레임 다시 요청한다.
    Application::Get().GetInputManager().SetCursorRequested(true);
}

void KnightsBoardController::OnDestroy()
{
    Application::Get().GetInputManager().SetCursorRequested(false);
}
