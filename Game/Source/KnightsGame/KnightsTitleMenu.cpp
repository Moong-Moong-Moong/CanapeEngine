#include "KnightsGame/KnightsTitleMenu.h"
#include "Scripts/UIDemoTextures.h"

#include <vector>

RTTR_REGISTRATION
{
    rttr::registration::class_<KnightsTitleMenu>("KnightsTitleMenu")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

using namespace Canape;

namespace
{
    constexpr UIColor kBackgroundColor{ 24, 28, 40, 255 };
    constexpr UIColor kTitleColor{ 255, 214, 102 };
    constexpr UIColor kStartColor{ 64, 156, 92 };
    constexpr UIColor kQuitColor{ 140, 80, 90 };

    using UIDemoTextures::kRoundedRadius;
    using UIDemoTextures::kRoundedSize;
}

UIButton* KnightsTitleMenu::AddMenuButton(UIElement& parent, std::string name, std::string text, float y)
{
    auto* button = parent.AddChild<UIButton>(std::move(name));
    button->SetAnchor({ 0.5f, 0.5f });
    button->SetPivot({ 0.5f, 0.5f });
    button->SetPosition({ 0.0f, y });
    button->SetSize({ 320.0f, 72.0f });
    button->SetTextureResource(m_RoundedTexture);
    button->SetNineSlice({ UIEdges(kRoundedRadius / kRoundedSize), UIEdges(14.0f) });

    auto* label = button->AddChild<UIText>("Label");
    label->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    label->SetSize({ 0.0f, 0.0f });
    label->SetText(std::move(text));
    label->SetFontSize(32.0f);
    label->SetHorizontalAlign(UITextAlign::Center);
    label->SetVerticalAlign(UITextAlign::Center);
    return button;
}

void KnightsTitleMenu::Start()
{
    const std::vector<uint8_t> rounded = UIDemoTextures::MakeRounded();
    m_RoundedTexture = Application::Get().GetRenderer().CreateTexture(kRoundedSize, kRoundedSize, TextureFormat::RGBA8_UNORM, rounded);

    auto* canvas = AddComponent<UICanvasComponent>(Vector2{ 1920.0f, 1080.0f });
    UIElement& root = canvas->GetRoot();

    //***** 전체 배경
    auto* background = root.AddChild<UIImage>("Background");
    background->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    background->SetSize({ 0.0f, 0.0f });
    background->SetColor(kBackgroundColor);

    //***** 제목
    auto* title = root.AddChild<UIText>("Title");
    title->SetAnchor({ 0.5f, 0.5f });
    title->SetPivot({ 0.5f, 0.5f });
    title->SetPosition({ 0.0f, -200.0f });
    title->SetSize({ 1200.0f, 140.0f });
    title->SetText("기사단 게임");
    title->SetFontSize(96.0f);
    title->SetColor(kTitleColor);
    title->SetHorizontalAlign(UITextAlign::Center);
    title->SetVerticalAlign(UITextAlign::Center);

    //***** 버튼
    UIButton* start = AddMenuButton(root, "StartButton", "게임 시작", 40.0f);
    start->SetColor(kStartColor);
    start->OnClicked = [this]
    {
        SceneManager& sceneManager = Application::Get().GetSceneManager();
        Scene* currentScene = GetOwnerObject()->GetScene();
        if (sceneManager.OpenScene("KnightsGame"))
        {
            sceneManager.UnloadScene(currentScene);
        }
    };

    UIButton* quit = AddMenuButton(root, "QuitButton", "종료", 140.0f);
    quit->SetColor(kQuitColor);
    quit->OnClicked = []
    {
        Application::Get().Quit();
    };

    Application::Get().GetInputManager().SetCursorRequested(true);
}

void KnightsTitleMenu::Update()
{
    // 씬 언로드가 지연되어 이전 씬의 OnDestroy(false)가 늦게 올 수 있으므로 매 프레임 다시 요청한다.
    Application::Get().GetInputManager().SetCursorRequested(true);
}

void KnightsTitleMenu::OnDestroy()
{
    Application::Get().GetInputManager().SetCursorRequested(false);
    if (m_RoundedTexture.IsValid())
    {
        Application::Get().GetRenderer().DestroyTexture(m_RoundedTexture);
        m_RoundedTexture = {};
    }
}
