#pragma once

#include "Asset/AssetHandle.h"
#include "Asset/AssetId.h"
#include "DX11Renderer/Data/MaterialDesc.h"

#include <utility>

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
        const TextureAssetHandle& GetBaseColorTexture() const { return m_BaseColorTexture; }

        void SetDesc(const MaterialDesc& desc) { m_Desc = desc; }
        void SetBaseColorTexture(TextureAssetHandle texture) { m_BaseColorTexture = std::move(texture); }

    private:
        static MaterialAssetId AllocateAssetId();

        const MaterialAssetId m_AssetId = AllocateAssetId();
        MaterialDesc m_Desc;
        TextureAssetHandle m_BaseColorTexture;
    };
}
