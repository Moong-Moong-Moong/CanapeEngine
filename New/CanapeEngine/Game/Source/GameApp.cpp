#include "GameApp.h"
#include "Scenes/SampleScene.h"

GameApp::GameApp()
    : Canape::Application({ L"Canape Game", 1280, 720 })
{
}

void GameApp::OnInit()
{
    SampleScene::Create(GetSceneManager());
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
