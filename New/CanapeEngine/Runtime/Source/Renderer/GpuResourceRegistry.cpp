#include "Renderer/GpuResourceRegistry.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Texture/TextureAsset.h"
#include "Asset/Material/MaterialAsset.h"
#include "../DX11MaterialResource.h"

namespace
{
    template <typename TAssetId, typename TResourceId, typename THash>
    TResourceId FindResourceId(const std::unordered_map<TAssetId, TResourceId, THash>& resources, TAssetId assetId)
    {
        auto it = resources.find(assetId);
        return it != resources.end() ? it->second : TResourceId{};
    }

    Canape::DX11AlphaMode ToDX11AlphaMode(Canape::AlphaMode alphaMode)
    {
        switch (alphaMode)
        {
        case Canape::AlphaMode::Masked: return Canape::DX11AlphaMode::Masked;
        case Canape::AlphaMode::Transparent: return Canape::DX11AlphaMode::Transparent;
        default: return Canape::DX11AlphaMode::Opaque;
        }
    }
}

bool Canape::GpuResourceRegistry::UpLoadMeshResource()
{
    // Gpu에 mesh resource 업로드를 요청함
    return true;
}

Canape::GpuResourceRegistry::GpuResourceRegistry(DX11Renderer& renderer)
    : m_Renderer(renderer)
{
}

Canape::MeshResourceId Canape::GpuResourceRegistry::CreateMeshResource(const MeshAsset& mesh)
{
    const MeshAssetId assetId = mesh.GetAssetId();
    const MeshResourceId existing = GetResourceId(assetId);
    if (existing.IsValid())
    {
        return existing;
    }

    const MeshResourceId resourceId = m_Renderer.CreateMeshResource(mesh.GetVertices().data(), mesh.GetVertexCount(),
        static_cast<uint32_t>(sizeof(StaticVertex)), mesh.GetIndices(), mesh.GetSubMeshes());
    if (!resourceId.IsValid())
    {
        return {};
    }

    m_mesh.emplace(assetId, resourceId);
    return resourceId;
}

Canape::TextureResourceId Canape::GpuResourceRegistry::CreateTextureResource(const TextureAsset& texture)
{
    const TextureAssetId assetId = texture.GetAssetId();
    const TextureResourceId existing = GetResourceId(assetId);
    if (existing.IsValid())
    {
        return existing;
    }

    if (texture.GetFormat() != TextureFormat::RGBA8_SRGB)
    {
        return {};
    }

    const TextureResourceId resourceId = m_Renderer.CreateTextureResource(texture.GetWidth(), texture.GetHeight(), texture.GetPixels());
    if (!resourceId.IsValid())
    {
        return {};
    }

    m_texture.emplace(assetId, resourceId);
    return resourceId;
}

Canape::MaterialResourceId Canape::GpuResourceRegistry::CreateMaterialResource(const MaterialAsset& material)
{
    const MaterialAssetId assetId = material.GetAssetId();
    const MaterialResourceId existing = GetResourceId(assetId);
    if (existing.IsValid())
    {
        return existing;
    }

    TextureResourceId baseColorTexture;
    if (const TextureAssetHandle& texture = material.GetBaseColorTexture(); texture.IsValid())
    {
        baseColorTexture = GetResourceId(texture.GetId());
        if (!baseColorTexture.IsValid())
        {
            return {};
        }
    }

    const MaterialDesc& source = material.GetDesc();
    DX11MaterialDesc desc;
    desc.baseColor = source.baseColor;
    desc.roughness = source.roughness;
    desc.metallic = source.metallic;
    desc.alphaMode = ToDX11AlphaMode(source.alphaMode);
    desc.alphaCutoff = source.alphaCutoff;

    const MaterialResourceId resourceId = m_Renderer.CreateMaterialResource(desc, baseColorTexture);
    if (!resourceId.IsValid())
    {
        return {};
    }

    m_material.emplace(assetId, resourceId);
    return resourceId;
}

Canape::MeshResourceId Canape::GpuResourceRegistry::GetResourceId(MeshAssetId assetId) const
{
    return FindResourceId(m_mesh, assetId);
}

Canape::TextureResourceId Canape::GpuResourceRegistry::GetResourceId(TextureAssetId assetId) const
{
    return FindResourceId(m_texture, assetId);
}

Canape::MaterialResourceId Canape::GpuResourceRegistry::GetResourceId(MaterialAssetId assetId) const
{
    return FindResourceId(m_material, assetId);
}

void Canape::GpuResourceRegistry::ReleaseMeshes(std::span<const MeshAssetId> meshes)
{
    for (const MeshAssetId assetId : meshes)
    {
        if (auto it = m_mesh.find(assetId); it != m_mesh.end())
        {
            m_Renderer.DestroyMeshResource(it->second);
            m_mesh.erase(it);
        }
    }
}

void Canape::GpuResourceRegistry::ReleaseTextures(std::span<const TextureAssetId> textures)
{
    for (const TextureAssetId assetId : textures)
    {
        if (auto it = m_texture.find(assetId); it != m_texture.end())
        {
            m_Renderer.DestroyTextureResource(it->second);
            m_texture.erase(it);
        }
    }
}

void Canape::GpuResourceRegistry::ReleaseMaterials(std::span<const MaterialAssetId> materials)
{
    for (const MaterialAssetId assetId : materials)
    {
        if (auto it = m_material.find(assetId); it != m_material.end())
        {
            m_Renderer.DestroyMaterialResource(it->second);
            m_material.erase(it);
        }
    }
}
