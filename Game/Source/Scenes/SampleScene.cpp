#include "Scenes/SampleScene.h"
#include "Scripts/CameraController.h"
#include "Scripts/PlayerController.h"

namespace
{
    constexpr char kPlayerModelPath[] = "Content/Models/Player.fbx";
    constexpr char kEnemyModelPath[] = "Content/Models/Enemy.fbx";
}

Canape::Scene* SampleScene::Create(Canape::SceneManager& sceneManager)
{
    Canape::Scene* scene = sceneManager.CreateScene(Name);

    Canape::GameObject* player = scene->CreateGameObject("Player");
    auto* playerRenderer = player->AddComponent<Canape::MeshRendererComponent>();
    playerRenderer->SetMesh(kPlayerModelPath);
    player->AddComponent<PlayerController>();

    const char* enemyNames[] = { "Enemy", "Enemy Transparent", "Enemy Masked" };
    for (uint32_t i = 0; i < 3; ++i)
    {
        Canape::GameObject* enemy = scene->CreateGameObject(enemyNames[i]);
        enemy->GetTransform()->SetLocalPosition({ -3.0f + 3.0f * i, 0.0f, 4.0f });
        enemy->GetTransform()->LookAt(player->GetTransform()->GetPosition());
        auto* renderer = enemy->AddComponent<Canape::MeshRendererComponent>();
        renderer->SetMesh(kEnemyModelPath);
    }

    Canape::GameObject* camera = scene->CreateGameObject("Main Camera");
    camera->AddComponent<CameraController>(player->GetHandle(), 35.0f);
    scene->SetMainCamera(camera->AddComponent<Canape::CameraComponent>());

    sceneManager.LoadScene(Name);
    return scene;
}
