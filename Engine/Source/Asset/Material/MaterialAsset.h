#pragma once

#include "Asset/AssetHandle.h"
#include "Asset/AssetId.h"
#include "MaterialData.h"
#include <array>
#include <utility>

namespace Canape
{
    using MaterialTextures = std::array<TextureAssetHandle, kMaterialTextureSlotCount>;

    // 생성/변경은 AssetManager(CreateMaterial, UpdateMaterial)를 통해서만 한다.
    class MaterialAsset
    {
    public:
        MaterialAsset(const MaterialDesc& desc, MaterialTextures textures);
        ~MaterialAsset();

        MaterialAsset(const MaterialAsset&) = delete;
        MaterialAsset& operator=(const MaterialAsset&) = delete;

        MaterialAssetId GetAssetId() const { return m_AssetId; }
        const MaterialDesc& GetDesc() const { return m_Desc; }
        const TextureAssetHandle& GetTexture(MaterialTextureSlot slot) const { return m_Textures[static_cast<size_t>(slot)]; }

        void SetDesc(const MaterialDesc& desc) { m_Desc = desc; }
        void SetTextures(MaterialTextures textures) { m_Textures = std::move(textures); }

    private:
        static MaterialAssetId AllocateAssetId();

        const MaterialAssetId m_AssetId = AllocateAssetId();
        MaterialDesc m_Desc;
        MaterialTextures m_Textures;
    };
}
