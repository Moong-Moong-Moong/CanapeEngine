#include "Scripts/SceneChanger.h"

void SceneChanger::Update()
{
    Canape::Application& application = Canape::Application::Get();
    if (!m_NextScene || !application.GetInputManager().IsKeyPressed(m_Key))
    {
        return;
    }

    Canape::SceneManager& sceneManager = application.GetSceneManager();
    const std::string currentScene = GetOwnerObject()->GetScene()->GetName();

    m_NextScene(sceneManager);
    sceneManager.UnloadScene(currentScene);
}
