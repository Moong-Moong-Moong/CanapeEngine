#include "Scenes/SampleScene2.h"
#include "Scenes/SampleScene.h"
#include "Scripts/SceneChanger.h"
#include "Scripts/GarbageCollector.h"
#include "../../../Engine/Source/Debug/DebugLog.h"

Canape::Scene* SampleScene2::Create(Canape::SceneManager& sceneManager)
{
    CN_LOG_GAME("init", "Sample Scene 2 로드중");
    Canape::Scene* scene = sceneManager.CreateScene(Name);

    Canape::GameObject* sceneChanger = scene->GetGameObject(scene->DeferAddGameObject("Scene Changer"));
    sceneChanger->AddComponent<SceneChanger>(&SampleScene::Create, Canape::Key::Space);

    Canape::GameObject* garbageCollector = scene->GetGameObject(scene->DeferAddGameObject("Garbage Collector"));
    garbageCollector->AddComponent<GarbageCollector>(Canape::Key::Delete);

    sceneManager.LoadScene(Name);
    CN_LOG_GAME("init", "Sample Scene 2 시작");
    return scene;
}
