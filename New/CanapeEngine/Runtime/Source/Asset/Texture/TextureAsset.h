#pragma once

#include "Asset/AssetId.h"
#include "Asset/Texture/TextureData.h"
#include <stdexcept>
#include <utility>
#include <vector>

namespace Canape
{
    class TextureAsset
    {
    public:
        TextureAsset(uint32_t width, uint32_t height, std::vector<uint8_t> pixels,
            TextureFormat format = TextureFormat::RGBA8_SRGB)
            : m_Width(width), m_Height(height), m_Format(format), m_Pixels(std::move(pixels))
        {
            if (const char* error = ValidateTextureData(width, height, format, m_Pixels.size()))
                throw std::invalid_argument(error);
        }
        TextureAsset(const TextureAsset&) = delete;
        TextureAsset& operator=(const TextureAsset&) = delete;

        TextureAssetId GetAssetId() const { return m_AssetId; }
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        TextureFormat GetFormat() const { return m_Format; }
        const std::vector<uint8_t>& GetPixels() const { return m_Pixels; }

    private:
        static TextureAssetId AllocateAssetId()
        {
            static uint64_t nextId = 1; // Single-threaded, process lifetime; never reused.
            if (nextId == 0) throw std::overflow_error("Texture asset ID space exhausted");
            return TextureAssetId{ nextId++ };
        }
        const TextureAssetId m_AssetId = AllocateAssetId();
        uint32_t m_Width;
        uint32_t m_Height;
        TextureFormat m_Format;
        std::vector<uint8_t> m_Pixels; // Immutable CPU data, no renderer/GPU ownership.
    };
}
