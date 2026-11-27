#include "KnightsGame/KnightsTitleMenu.h"
#include "KnightsGame/KnightsUi.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<KnightsTitleMenu>("KnightsTitleMenu")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

using namespace Canape;

UIButton* KnightsTitleMenu::AddMenuButton(UIElement& parent, std::string name, std::string text, float y)
{
    auto* button = KnightsUi::AddButton(parent, std::move(name), m_RoundedTexture, std::move(text), 32.0f, { 0.0f, y },
                                        { 320.0f, 72.0f }, KnightsUi::kNeutralButtonColor);
    button->SetAnchor({ 0.5f, 0.5f });
    button->SetPivot({ 0.5f, 0.5f });
    return button;
}

void KnightsTitleMenu::Start()
{
    m_RoundedTexture = KnightsUi::CreateRoundedTexture();

    auto* canvas = AddComponent<UICanvasComponent>(Vector2{ 1920.0f, 1080.0f });
    UIElement& root = canvas->GetRoot();

    //***** 전체 배경
    KnightsUi::AddBackground(root);

    //***** 제목
    auto* title = KnightsUi::AddLabel(root, "Title", "기사단 게임", 96.0f, { 0.0f, -200.0f }, { 1200.0f, 140.0f },
                                      KnightsUi::kTitleColor, UITextAlign::Center, UITextAlign::Center);
    title->SetAnchor({ 0.5f, 0.5f });
    title->SetPivot({ 0.5f, 0.5f });

    //***** 버튼
    UIButton* start = AddMenuButton(root, "StartButton", "게임 시작", 40.0f);
    start->SetColor(KnightsUi::kConfirmColor);
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
    quit->SetColor(KnightsUi::kQuitColor);
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
    KnightsUi::DestroyTexture(m_RoundedTexture);
}
