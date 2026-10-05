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
    Canape::ResourceManager& resources = Canape::Application::Get().GetResourceManager();
    Canape::Scene* scene = sceneManager.CreateScene(Name);

    Canape::GameObject* player = scene->CreateGameObject("Player");
    player->AddComponent<Canape::MeshRendererComponent>(resources.LoadMesh(kPlayerModelPath));
    player->AddComponent<PlayerController>();

    Canape::GameObject* enemy = scene->CreateGameObject("Enemy");
    enemy->GetTransform()->SetLocalPosition({ 3.0f, 0.0f, 4.0f });
    enemy->GetTransform()->LookAt(player->GetTransform()->GetPosition());
    enemy->AddComponent<Canape::MeshRendererComponent>(resources.LoadMesh(kEnemyModelPath));

    Canape::GameObject* camera = scene->CreateGameObject("Main Camera");
    camera->AddComponent<CameraController>(player->GetHandle());
    scene->SetMainCamera(camera->AddComponent<Canape::CameraComponent>());

    sceneManager.LoadScene(Name);
    return scene;
}
