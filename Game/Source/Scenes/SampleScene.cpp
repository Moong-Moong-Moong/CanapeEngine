#include "Scenes/SampleScene.h"
#include "Scenes/SampleScene2.h"
#include "Scripts/CameraController.h"
#include "Scripts/GarbageCollector.h"
#include "Scripts/PlayerController.h"
#include "Scripts/SceneChanger.h"
#include "../../../Engine/Source/Debug/DebugLog.h"

namespace
{
    constexpr char kPlayerModelPath[] = "Content/Models/Player.fbx";
    constexpr char kEnemyModelPath[] = "Content/Models/Enemy.fbx";
    constexpr char kLevelModelPath[] = "Content/Models/Level.fbx";
    constexpr char kPlayerTexturePath[] = "Content/Texture/Player_BaseColor.png";
    constexpr char kEnemyTexturePath[] = "Content/Texture/Enemy_BaseColor.png";
}

Canape::Scene* SampleScene::Create(Canape::SceneManager& sceneManager)
{
    CN_LOG_GAME("init", "Sample Scene 1 로드중");
    Canape::Scene* scene = sceneManager.CreateScene(Name);

    Canape::AssetManager& assets = Canape::Application::Get().GetAssetManager();
    const Canape::AssetOwnerInfo materialOwner{ Name, "SampleScene", "MaterialSetup" };

    const Canape::TextureAssetHandle playerTexture = assets.GetTextureHandle(kPlayerTexturePath, materialOwner);
    const Canape::TextureAssetHandle enemyTexture = assets.GetTextureHandle(kEnemyTexturePath, materialOwner);

    Canape::MaterialDesc transparentDesc;
    transparentDesc.baseColor = { 1.0f, 1.0f, 1.0f, 0.5f };
    transparentDesc.alphaMode = Canape::AlphaMode::Transparent;

    Canape::MaterialDesc maskedDesc;
    maskedDesc.alphaMode = Canape::AlphaMode::Masked;
    maskedDesc.alphaCutoff = 0.5f;

    assets.CreateMaterial("M_Player", Canape::MaterialDesc{}, playerTexture, materialOwner);
    assets.CreateMaterial("M_Enemy", Canape::MaterialDesc{}, enemyTexture, materialOwner);
    assets.CreateMaterial("M_Enemy_Transparent", transparentDesc, enemyTexture, materialOwner);
    assets.CreateMaterial("M_Enemy_Masked", maskedDesc, enemyTexture, materialOwner);

    Canape::GameObject* player = scene->CreateGameObject("Player");
    auto* playerRenderer = player->AddComponent<Canape::MeshRendererComponent>();
    playerRenderer->SetMesh(kPlayerModelPath);
    playerRenderer->SetMaterial(0, "M_Player");
    player->AddComponent<PlayerController>();

    const char* enemyNames[] = { "Enemy", "Enemy Transparent", "Enemy Masked" };
    const char* enemyMaterials[] = { "M_Enemy", "M_Enemy_Transparent", "M_Enemy_Masked" };
    for (uint32_t i = 0; i < 3; ++i)
    {
        Canape::GameObject* enemy = scene->CreateGameObject(enemyNames[i]);
        enemy->GetTransform()->SetLocalPosition({ -3.0f + 3.0f * i, 0.0f, 4.0f });
        enemy->GetTransform()->LookAt(player->GetTransform()->GetPosition());
        auto* renderer = enemy->AddComponent<Canape::MeshRendererComponent>();
        renderer->SetMesh(kEnemyModelPath);
        renderer->SetMaterial(0, enemyMaterials[i]);
    }

    Canape::GameObject* camera = scene->CreateGameObject("Main Camera");
    camera->AddComponent<CameraController>(player->GetHandle(), 35.0f);
    scene->SetMainCamera(camera->AddComponent<Canape::CameraComponent>());

    Canape::GameObject* sceneChanger = scene->CreateGameObject("Scene Changer");
    sceneChanger->AddComponent<SceneChanger>(&SampleScene2::Create, Canape::Key::Space);

    Canape::GameObject* garbageCollector = scene->CreateGameObject("Garbage Collector");
    garbageCollector->AddComponent<GarbageCollector>(Canape::Key::Delete);


    Canape::GameObject* level = scene->CreateGameObject("Level");
    auto* levelRenderer = level->AddComponent<Canape::MeshRendererComponent>();
    levelRenderer->SetMesh(kLevelModelPath);

    level->GetTransform()->SetLocalPosition({ 0, -2, 0 });

    sceneManager.LoadScene(Name);
    CN_LOG_GAME("init", "Sample Scene 1 시작");
    return scene;
}
