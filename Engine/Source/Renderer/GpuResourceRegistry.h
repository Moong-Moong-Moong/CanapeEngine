#pragma once

#include "Asset/AssetId.h"
#include "DX11Renderer/Resource/DX11ResourceId.h"

#include <span>
#include <unordered_map>

namespace Canape
{
    class DX11Renderer;

    //***** 에셋 ID와 DX11Renderer가 발급한 GPU 리소스 ID를 연결하는 클래스
    class GpuResourceRegistry
    {
    public:
        explicit GpuResourceRegistry(DX11Renderer& renderer);

        GpuResourceRegistry(const GpuResourceRegistry&) = delete;
        GpuResourceRegistry& operator=(const GpuResourceRegistry&) = delete;

        MeshResourceId CreateMeshResource(const MeshAsset& mesh);
        TextureResourceId CreateTextureResource(const TextureAsset& texture);
        MaterialResourceId CreateMaterialResource(const MaterialAsset& material);

        // 해제되었거나 무효화된 리소스는 빈 핸들을 반환한다.
        MeshResourceId GetResourceId(MeshAssetId assetId) const;
        TextureResourceId GetResourceId(TextureAssetId assetId) const;
        MaterialResourceId GetResourceId(MaterialAssetId assetId) const;

        void ReleaseMeshes(std::span<const MeshAssetId> meshes);
        void ReleaseTextures(std::span<const TextureAssetId> textures);
        void ReleaseMaterials(std::span<const MaterialAssetId> materials);

    private:
        DX11Renderer& m_Renderer;

        std::unordered_map<MeshAssetId, MeshResourceId, AssetIdHash<MeshAsset>> m_Meshes;
        std::unordered_map<TextureAssetId, TextureResourceId, AssetIdHash<TextureAsset>> m_Textures;
        std::unordered_map<MaterialAssetId, MaterialResourceId, AssetIdHash<MaterialAsset>> m_Materials;
    };
}
