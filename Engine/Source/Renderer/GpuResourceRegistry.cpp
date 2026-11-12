#include "Renderer/GpuResourceRegistry.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Texture/CubemapAsset.h"
#include "Asset/Texture/TextureAsset.h"
#include "DX11Renderer/DX11Renderer.h"

namespace Canape
{
    namespace
    {
        template <typename TAssetId, typename TResourceId, typename THash>
        TResourceId FindResourceId(const std::unordered_map<TAssetId, TResourceId, THash>& resources, TAssetId assetId)
        {
            const auto it = resources.find(assetId);
            return it != resources.end() ? it->second : TResourceId{};
        }
    }

    GpuResourceRegistry::GpuResourceRegistry(DX11Renderer& renderer)
        : m_Renderer(renderer)
    {
    }

    MeshResourceId GpuResourceRegistry::CreateMeshResource(const MeshAsset& mesh)
    {
        const MeshAssetId assetId = mesh.GetAssetId();
        if (const MeshResourceId existing = GetResourceId(assetId); existing.IsValid())
        {
            return existing;
        }

        // 실패한 업로드가 이전 등록을 남기지 않도록 먼저 지운다.
        m_Meshes.erase(assetId);
        const MeshResourceId resourceId = m_Renderer.CreateMesh(std::span<const StaticVertex>(mesh.GetVertices()),
            mesh.GetIndices(), mesh.GetSubMeshes());
        if (resourceId.IsValid())
        {
            m_Meshes.emplace(assetId, resourceId);
        }
        return resourceId;
    }

    TextureResourceId GpuResourceRegistry::CreateTextureResource(const TextureAsset& texture)
    {
        const TextureAssetId assetId = texture.GetAssetId();
        if (const TextureResourceId existing = GetResourceId(assetId); existing.IsValid())
        {
            return existing;
        }

        m_Textures.erase(assetId);
        const TextureResourceId resourceId = m_Renderer.CreateTexture(texture.GetWidth(), texture.GetHeight(),
            texture.GetFormat(), texture.GetPixels());
        if (resourceId.IsValid())
        {
            m_Textures.emplace(assetId, resourceId);
        }
        return resourceId;
    }

    CubemapResourceId GpuResourceRegistry::CreateCubemapResource(const CubemapAsset& cubemap)
    {
        const CubemapAssetId assetId = cubemap.GetAssetId();
        if (const CubemapResourceId existing = GetResourceId(assetId); existing.IsValid())
        {
            return existing;
        }

        m_Cubemaps.erase(assetId);
        const CubemapResourceId resourceId = m_Renderer.CreateCubemap(cubemap.GetFaceSize(), cubemap.GetMipLevels(),
            cubemap.GetFormat(), cubemap.GetPixels());
        if (resourceId.IsValid())
        {
            m_Cubemaps.emplace(assetId, resourceId);
        }
        return resourceId;
    }

    MaterialResourceId GpuResourceRegistry::CreateMaterialResource(const MaterialAsset& material)
    {
        const MaterialAssetId assetId = material.GetAssetId();
        if (const MaterialResourceId existing = GetResourceId(assetId); existing.IsValid())
        {
            return existing;
        }

        // 핸들이 있는데 GPU 리소스가 없으면 실패. 핸들이 없으면 빈 ID(렌더러 기본값 사용).
        bool missing = false;
        auto resolve = [&](MaterialTextureSlot slot) -> TextureResourceId
        {
            const TextureAssetHandle& texture = material.GetTexture(slot);
            if (!texture.IsValid())
            {
                return {};
            }
            const TextureResourceId resourceId = GetResourceId(texture.GetId());
            missing |= !resourceId.IsValid();
            return resourceId;
        };

        const TextureResourceId baseColorTexture = resolve(MaterialTextureSlot::BaseColor);
        // 셰이더 ORM 텍스처는 G = roughness, B = metallic만 읽으므로 glTF metallicRoughness 텍스처를 그대로 쓴다.
        // Normal/Occlusion/Emissive는 아직 렌더러 머티리얼에 슬롯이 없어 GPU로 넘기지 않는다.
        const TextureResourceId ormTexture = resolve(MaterialTextureSlot::MetallicRoughness);
        if (missing)
        {
            return {};
        }

        m_Materials.erase(assetId);
        const MaterialResourceId resourceId = m_Renderer.CreateMaterial(material.GetDesc(), baseColorTexture, ormTexture);
        if (resourceId.IsValid())
        {
            m_Materials.emplace(assetId, resourceId);
        }
        return resourceId;
    }

    MeshResourceId GpuResourceRegistry::GetResourceId(MeshAssetId assetId) const
    {
        const MeshResourceId resourceId = FindResourceId(m_Meshes, assetId);
        return m_Renderer.IsMeshValid(resourceId) ? resourceId : MeshResourceId{};
    }

    TextureResourceId GpuResourceRegistry::GetResourceId(TextureAssetId assetId) const
    {
        const TextureResourceId resourceId = FindResourceId(m_Textures, assetId);
        return m_Renderer.IsTextureValid(resourceId) ? resourceId : TextureResourceId{};
    }

    CubemapResourceId GpuResourceRegistry::GetResourceId(CubemapAssetId assetId) const
    {
        const CubemapResourceId resourceId = FindResourceId(m_Cubemaps, assetId);
        return m_Renderer.IsCubemapValid(resourceId) ? resourceId : CubemapResourceId{};
    }

    MaterialResourceId GpuResourceRegistry::GetResourceId(MaterialAssetId assetId) const
    {
        // 텍스처 해제 시 DX11Renderer가 참조 머티리얼을 무효화하므로 매번 유효성을 확인한다.
        const MaterialResourceId resourceId = FindResourceId(m_Materials, assetId);
        return m_Renderer.IsMaterialValid(resourceId) ? resourceId : MaterialResourceId{};
    }

    void GpuResourceRegistry::ReleaseMeshes(std::span<const MeshAssetId> meshes)
    {
        for (const MeshAssetId assetId : meshes)
        {
            if (const auto it = m_Meshes.find(assetId); it != m_Meshes.end())
            {
                m_Renderer.DestroyMesh(it->second);
                m_Meshes.erase(it);
            }
        }
    }

    void GpuResourceRegistry::ReleaseTextures(std::span<const TextureAssetId> textures)
    {
        for (const TextureAssetId assetId : textures)
        {
            if (const auto it = m_Textures.find(assetId); it != m_Textures.end())
            {
                m_Renderer.DestroyTexture(it->second);
                m_Textures.erase(it);
            }
        }
    }

    void GpuResourceRegistry::ReleaseCubemaps(std::span<const CubemapAssetId> cubemaps)
    {
        for (const CubemapAssetId assetId : cubemaps)
        {
            if (const auto it = m_Cubemaps.find(assetId); it != m_Cubemaps.end())
            {
                m_Renderer.DestroyCubemap(it->second);
                m_Cubemaps.erase(it);
            }
        }
    }

    void GpuResourceRegistry::ReleaseMaterials(std::span<const MaterialAssetId> materials)
    {
        for (const MaterialAssetId assetId : materials)
        {
            if (const auto it = m_Materials.find(assetId); it != m_Materials.end())
            {
                m_Renderer.DestroyMaterial(it->second);
                m_Materials.erase(it);
            }
        }
    }
}
