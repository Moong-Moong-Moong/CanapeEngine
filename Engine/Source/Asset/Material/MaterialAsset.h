#pragma once

#include "Asset/AssetHandle.h"
#include "Asset/AssetId.h"
#include "Asset/Material/MaterialDesc.h"

namespace Canape
{
    // Immutable after creation. Created only by AssetManager::CreateMaterial.
    class MaterialAsset
    {
    public:
        MaterialAsset(const MaterialDesc& desc, TextureAssetHandle baseColorTexture);
        ~MaterialAsset();

        MaterialAsset(const MaterialAsset&) = delete;
        MaterialAsset& operator=(const MaterialAsset&) = delete;

        MaterialAssetId GetAssetId() const { return m_AssetId; }
        const MaterialDesc& GetDesc() const { return m_Desc; }
        // The material's own reference; invalid when it has no BaseColor texture.
        // Resolve through AssetManager::GetTexture.
        const TextureAssetHandle& GetBaseColorTexture() const { return m_BaseColorTexture; }

    private:
        static MaterialAssetId AllocateAssetId();

        const MaterialAssetId m_AssetId = AllocateAssetId();
        MaterialDesc m_Desc;
        TextureAssetHandle m_BaseColorTexture;
    };
}
