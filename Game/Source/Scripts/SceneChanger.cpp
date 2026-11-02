#include "Scripts/SceneChanger.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<SceneChanger>("SceneChanger")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("nextScene", &SceneChanger::m_NextScene)
        .property("key", &SceneChanger::m_Key);
}

void SceneChanger::Update()
{
    Canape::Application& application = Canape::Application::Get();
    if (m_NextScene.empty() || !application.GetInputManager().IsKeyPressed(m_Key))
    {
        return;
    }

    Canape::SceneManager& sceneManager = application.GetSceneManager();
    Canape::Scene* currentScene = GetOwnerObject()->GetScene();

    if (sceneManager.OpenScene(m_NextScene))
    {
        sceneManager.UnloadScene(currentScene);
    }
}
