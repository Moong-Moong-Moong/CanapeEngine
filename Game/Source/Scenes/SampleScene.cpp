#include "Scenes/SampleScene.h"
#include "Scripts/CameraController.h"
#include "Scripts/PlayerController.h"

namespace
{
    constexpr char kPlayerModelPath[] = "Content/Models/Player.fbx";
    constexpr char kEnemyModelPath[] = "Content/Models/Enemy.fbx";
    constexpr char kPlayerTexturePath[] = "Content/Texture/Player_BaseColor.png";
    constexpr char kEnemyTexturePath[] = "Content/Texture/Enemy_BaseColor.png";

    void SetModelMaterial(Canape::MeshRendererComponent& renderer,
        const std::shared_ptr<Canape::MaterialAsset>& material)
    {
        if (const auto& mesh = renderer.GetMesh())
        {
            for (const Canape::SubMesh& submesh : mesh->GetSubMeshes())
                renderer.SetMaterial(submesh.materialSlot, material);
        }
    }
}

Canape::Scene* SampleScene::Create(Canape::SceneManager& sceneManager)
{
    Canape::ResourceManager& resources = Canape::Application::Get().GetResourceManager();
    Canape::Scene* scene = sceneManager.CreateScene(Name);

    auto playerMaterial = std::make_shared<Canape::MaterialAsset>();
    playerMaterial->SetBaseColorTexture(resources.LoadTexture(kPlayerTexturePath));

    Canape::GameObject* player = scene->CreateGameObject("Player");
    auto* playerRenderer = player->AddComponent<Canape::MeshRendererComponent>(resources.LoadMesh(kPlayerModelPath));
    SetModelMaterial(*playerRenderer, playerMaterial);
    player->AddComponent<PlayerController>();

    const auto enemyMesh = resources.LoadMesh(kEnemyModelPath);
    const auto enemyTexture = resources.LoadTexture(kEnemyTexturePath);
    auto maskedTexture = enemyTexture;
    if (enemyTexture)
    {
        // Sample-only alpha holes. Preserve the original RGB and texture used by the other modes.
        auto pixels = enemyTexture->GetPixels();
        const uint32_t width = enemyTexture->GetWidth(), height = enemyTexture->GetHeight();
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
                if ((x * 6 / width + y * 6 / height) % 3 == 0)
                    pixels[(size_t(y) * width + x) * 4 + 3] = 0;
        maskedTexture = std::make_shared<Canape::TextureAsset>(width, height, std::move(pixels), enemyTexture->GetFormat());
    }

    const char* enemyNames[] = { "Enemy", "Enemy Transparent", "Enemy Masked" };
    const Canape::AlphaMode modes[] = { Canape::AlphaMode::Opaque, Canape::AlphaMode::Transparent, Canape::AlphaMode::Masked };
    for (uint32_t i = 0; i < 3; ++i)
    {
        Canape::MaterialDesc desc;
        desc.alphaMode = modes[i];
        if (modes[i] == Canape::AlphaMode::Transparent) desc.baseColor.W = 0.25f;
        auto material = std::make_shared<Canape::MaterialAsset>(desc);
        material->SetBaseColorTexture(modes[i] == Canape::AlphaMode::Masked ? maskedTexture : enemyTexture);

        Canape::GameObject* enemy = scene->CreateGameObject(enemyNames[i]);
        enemy->GetTransform()->SetLocalPosition({ -3.0f + 3.0f * i, 0.0f, 4.0f });
        enemy->GetTransform()->LookAt(player->GetTransform()->GetPosition());
        auto* renderer = enemy->AddComponent<Canape::MeshRendererComponent>(enemyMesh);
        SetModelMaterial(*renderer, material);
    }

    Canape::GameObject* camera = scene->CreateGameObject("Main Camera");
    camera->AddComponent<CameraController>(player->GetHandle(), 35.0f);
    scene->SetMainCamera(camera->AddComponent<Canape::CameraComponent>());

    sceneManager.LoadScene(Name);
    return scene;
}
