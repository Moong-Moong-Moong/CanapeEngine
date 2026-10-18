#include "Renderer/Renderer.h"
#include "Renderer/RenderScene.h"
#include "DX11Renderer/DX11Renderer.h"
#include "Asset/AssetManager.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Texture/TextureAsset.h"
#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/MeshRendererComponent.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace Canape
{
    Renderer::Renderer()
        : m_Backend(std::make_unique<DX11Renderer>())
    {
    }

    Renderer::~Renderer() = default;

    bool Renderer::Initialize(void* nativeWindow, uint32_t width, uint32_t height)
    {
        return m_Backend->Initialize(nativeWindow, width, height);
    }

    bool Renderer::UploadMesh(const MeshAsset& mesh, std::string& error)
    {
        const MeshHandle handle = m_Backend->RegisterMesh(mesh.GetAssetId(), mesh.GetVertices(),
            mesh.GetIndices(), mesh.GetSubMeshes());
        if (!handle.IsValid())
        {
            error = m_Backend->GetLastError();
            return false;
        }
        error.clear();
        return true;
    }

    bool Renderer::UploadTexture(const TextureAsset& texture, std::string& error)
    {
        const TextureHandle handle = m_Backend->RegisterTexture(texture.GetAssetId(), texture.GetWidth(),
            texture.GetHeight(), texture.GetFormat(), texture.GetPixels());
        if (!handle.IsValid())
        {
            error = m_Backend->GetLastError();
            return false;
        }
        error.clear();
        return true;
    }

    bool Renderer::UploadMaterial(const MaterialAsset& material, std::string& error)
    {
        TextureHandle baseColorTexture{};
        if (const TextureAssetHandle& texture = material.GetBaseColorTexture(); texture.IsValid())
        {
            baseColorTexture = m_Backend->FindTexture(texture.GetId());
            if (!baseColorTexture.IsValid())
            {
                error = "BaseColor texture must be uploaded before the material";
                return false;
            }
        }

        const MaterialHandle handle = m_Backend->RegisterMaterial(material.GetAssetId(), material.GetDesc(), baseColorTexture);
        if (!handle.IsValid())
        {
            error = m_Backend->GetLastError();
            return false;
        }
        error.clear();
        return true;
    }

    void Renderer::ReleaseMeshes(std::span<const MeshAssetId> meshes)
    {
        for (const MeshAssetId assetId : meshes)
        {
            if (const MeshHandle handle = m_Backend->FindMesh(assetId); handle.IsValid())
            {
                m_Backend->DestroyMesh(handle);
            }
        }
    }

    void Renderer::ReleaseTextures(std::span<const TextureAssetId> textures)
    {
        for (const TextureAssetId assetId : textures)
        {
            if (const TextureHandle handle = m_Backend->FindTexture(assetId); handle.IsValid())
            {
                m_Backend->DestroyTexture(handle);
            }
        }
    }

    void Renderer::ReleaseMaterials(std::span<const MaterialAssetId> materials)
    {
        for (const MaterialAssetId assetId : materials)
        {
            if (const MaterialHandle handle = m_Backend->FindMaterial(assetId); handle.IsValid())
            {
                m_Backend->DestroyMaterial(handle);
            }
        }
    }

    void Renderer::Render(Scene* scene, const AssetManager& assets)
    {
        if (!m_Backend->BeginFrame())
        {
            return;
        }

        CameraComponent* camera = scene ? scene->GetMainCamera() : nullptr;
        RenderScene renderScene;

        if (!camera || !camera->IsActiveAndEnabled())
        {
            if (!m_WarnedNoCamera)
            {
                OutputDebugStringA("[Engine] No main camera is rendering\n");
                m_WarnedNoCamera = true;
            }
        }
        else
        {
            m_WarnedNoCamera = false;

            const float aspectRatio = static_cast<float>(m_Backend->GetWidth()) / static_cast<float>(m_Backend->GetHeight());
            renderScene.camera.view = camera->GetViewMatrix();
            renderScene.camera.projection = camera->GetProjectionMatrix(aspectRatio);
            renderScene.camera.cameraPosition = camera->GetTransform()->GetPosition();

            DX11Renderer& backend = *m_Backend;
            scene->ForEachGameObject([&backend, &assets, &renderScene](GameObject* gameObject)
            {
                if (gameObject->IsPendingDestroy() || !gameObject->IsActiveInHierarchy())
                {
                    return;
                }

                for (MeshRendererComponent* meshRenderer : gameObject->GetComponents<MeshRendererComponent>())
                {
                    const MeshAsset* mesh = assets.GetMesh(meshRenderer->GetMesh());
                    if (!meshRenderer->IsEnabled() || !mesh)
                    {
                        continue;
                    }

                    // Uploaded by AssetManager at load time; never registered here.
                    const MeshHandle handle = backend.FindMesh(mesh->GetAssetId());
                    if (!handle.IsValid())
                    {
                        continue;
                    }

                    RenderItem item;
                    item.mesh = handle;
                    item.world = gameObject->GetTransform()->GetLocalToWorldMatrix();
                    for (uint32_t i = 0; i < mesh->GetSubMeshes().size(); ++i)
                    {
                        item.submeshIndex = i;
                        item.material = {};
                        // Uploaded by AssetManager::CreateMaterial; a missing material draws default white.
                        if (const MaterialAsset* material = assets.GetMaterial(meshRenderer->GetMaterial(mesh->GetSubMeshes()[i].materialSlot)))
                        {
                            item.material = backend.FindMaterial(material->GetAssetId());
                        }
                        renderScene.items.push_back(item);
                    }
                }
            });
        }

        m_Backend->DrawScene(renderScene);
        m_Backend->EndFrame();
    }

    uint32_t Renderer::GetWidth() const
    {
        return m_Backend->GetWidth();
    }

    uint32_t Renderer::GetHeight() const
    {
        return m_Backend->GetHeight();
    }

    const std::string& Renderer::GetLastError() const
    {
        return m_Backend->GetLastError();
    }
}
