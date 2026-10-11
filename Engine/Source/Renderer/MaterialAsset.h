#pragma once

#include "MaterialData.h"
#include "Renderer/TextureAsset.h"
#include <memory>
#include <stdexcept>

namespace Canape
{
    class MaterialAsset
    {
    public:
        explicit MaterialAsset(const MaterialDesc& desc = {}) : m_Desc(desc) {}
        MaterialAsset(const MaterialAsset&) = delete;
        MaterialAsset& operator=(const MaterialAsset&) = delete;

        MaterialAssetId GetAssetId() const { return m_AssetId; }
        const MaterialDesc& GetDesc() const { return m_Desc; }
        // CPU edit only: the renderer keeps its first registered snapshot until explicit removal/reset.
        void SetDesc(const MaterialDesc& desc) { m_Desc = desc; }
        const std::shared_ptr<TextureAsset>& GetBaseColorTexture() const { return m_BaseColorTexture; }
        // Also CPU-only: explicit material removal/re-registration applies a changed texture reference.
        void SetBaseColorTexture(std::shared_ptr<TextureAsset> texture) { m_BaseColorTexture = std::move(texture); }

    private:
        static MaterialAssetId AllocateAssetId()
        {
            static uint64_t nextId = 1; // Single-threaded; never reset or reused during this process.
            if (nextId == 0) throw std::overflow_error("Material asset ID space exhausted");
            return MaterialAssetId{ nextId++ };
        }

        const MaterialAssetId m_AssetId = AllocateAssetId();
        MaterialDesc m_Desc;
        std::shared_ptr<TextureAsset> m_BaseColorTexture;
    };
}
