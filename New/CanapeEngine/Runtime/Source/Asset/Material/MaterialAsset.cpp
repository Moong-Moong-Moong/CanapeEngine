#include "Asset/Material/MaterialAsset.h"
#include "Asset/AssetManager.h"

#include <stdexcept>
#include <utility>

namespace Canape
{
    MaterialAsset::MaterialAsset(const MaterialDesc& desc, TextureAssetHandle baseColorTexture)
        : m_Desc(desc), m_BaseColorTexture(std::move(baseColorTexture))
    {
    }

    MaterialAsset::~MaterialAsset() = default;

    MaterialAssetId MaterialAsset::AllocateAssetId()
    {
        static uint64_t nextId = 1; 
        if (nextId == 0) throw std::overflow_error("Material asset ID space exhausted");
        return MaterialAssetId{ nextId++ };
    }
}
