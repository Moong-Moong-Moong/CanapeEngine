#pragma once

#include "Asset/AssetId.h"
#include "DX11Renderer/Data/TextureData.h"
#include <stdexcept>
#include <utility>
#include <vector>

namespace Canape
{
    class CubemapAsset
    {
    public:
        CubemapAsset(uint32_t faceSize, uint32_t mipLevels, std::vector<uint8_t> pixels,
            TextureFormat format = TextureFormat::RGBA8_SRGB)
            : m_FaceSize(faceSize), m_MipLevels(mipLevels), m_Format(format), m_Pixels(std::move(pixels))
        {
            if (const char* error = ValidateCubemapData(faceSize, mipLevels, format, m_Pixels.size()))
                throw std::invalid_argument(error);
        }
        CubemapAsset(const CubemapAsset&) = delete;
        CubemapAsset& operator=(const CubemapAsset&) = delete;

        CubemapAssetId GetAssetId() const { return m_AssetId; }
        uint32_t GetFaceSize() const { return m_FaceSize; }
        uint32_t GetMipLevels() const { return m_MipLevels; }
        TextureFormat GetFormat() const { return m_Format; }
        const std::vector<uint8_t>& GetPixels() const { return m_Pixels; }

    private:
        static CubemapAssetId AllocateAssetId()
        {
            static uint64_t nextId = 1;
            if (nextId == 0) throw std::overflow_error("Cubemap asset ID space exhausted");
            return CubemapAssetId{ nextId++ };
        }
        const CubemapAssetId m_AssetId = AllocateAssetId();
        uint32_t m_FaceSize;
        uint32_t m_MipLevels;
        TextureFormat m_Format;
        std::vector<uint8_t> m_Pixels;
    };
}
