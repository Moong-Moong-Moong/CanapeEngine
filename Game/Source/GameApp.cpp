#include "GameApp.h"
#include "Scenes/SampleScene.h"
#include "Scenes/SampleScene2.h"
#include "CNDebug/DebugLog.h"

GameApp::GameApp()
    : Canape::Application({ L"Canape Game", 1280, 720 })
{
}

void GameApp::OnInit()
{
#if defined(CANAPE_GAME_CONTENT_DIR)
    GetSceneManager().SetSceneDirectory(CANAPE_GAME_CONTENT_DIR "Scenes/");
#endif

    //Canape::SceneManager& sceneManager = GetSceneManager();
    //sceneManager.RegisterSceneFactory(SampleScene::Name, &SampleScene::Create);
    //sceneManager.RegisterSceneFactory(SampleScene2::Name, &SampleScene2::Create);
    //sceneManager.OpenScene(SampleScene::Name);

    GetCommands().Register("asset.release", "asset.release", "사용하지 않는 에셋 해제", 0,
        [this](const Canape::CommandArgs&)
        {
            const size_t released = GetAssetManager().ReleaseUnusedAssets();
            CN_LOG_GAME("Console", "미사용 에셋 해제: ", released, "개");
        });
}

void GameApp::OnUpdate(float deltaTime)
{
    (void)deltaTime;

    const Canape::InputManager& input = GetInputManager();
    if (input.IsKeyPressed(Canape::Key::Escape) || input.IsGamepadPressed(Canape::GamepadButton::Back))
    {
        Quit();
    }
}

void GameApp::OnShutdown()
{
}
