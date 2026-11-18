#pragma once

#include <cstddef>
#include <cstdint>

namespace Canape
{
    // Pixel storage is tightly packed. Float formats and UNORM are linear; sRGB decodes on sampling.
    enum class TextureFormat { RGBA8_SRGB, RGBA8_UNORM, RGBA16_FLOAT, RGBA32_FLOAT };

    inline uint32_t CubemapBytesPerPixel(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::RGBA8_SRGB:
        case TextureFormat::RGBA8_UNORM: return 4;
        case TextureFormat::RGBA16_FLOAT: return 8;
        case TextureFormat::RGBA32_FLOAT: return 16;
        default: return 0;
        }
    }

    inline const char* ValidateTextureData(uint32_t width, uint32_t height, TextureFormat format, size_t byteCount)
    {
        if (format != TextureFormat::RGBA8_SRGB && format != TextureFormat::RGBA8_UNORM)
            return "Unsupported texture pixel format";
        if (width == 0 || height == 0) return "Texture dimensions must be nonzero";
        const uint64_t pixels = uint64_t(width) * height;
        if (pixels > SIZE_MAX / 4 || byteCount != pixels * 4)
            return "Texture requires tightly packed width * height * 4 RGBA bytes";
        return nullptr;
    }

    inline const char* ValidateCubemapData(uint32_t faceSize, uint32_t mipLevels, TextureFormat format, size_t byteCount)
    {
        const uint32_t bytesPerPixel = CubemapBytesPerPixel(format);
        if (bytesPerPixel == 0) return "Unsupported cubemap pixel format";
        if (faceSize == 0) return "Cubemap face size must be nonzero";
        if (mipLevels == 0 || mipLevels > 32 || (faceSize >> (mipLevels - 1)) == 0)
            return "Cubemap mip count exceeds the face size chain";
        uint64_t bytes = 0;
        for (uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            const uint64_t size = faceSize >> mip;
            bytes += size * size * bytesPerPixel;
        }
        bytes *= 6;
        if (bytes > SIZE_MAX || byteCount != bytes)
            return "Cubemap requires tightly packed pixels for 6 faces and every mip";
        return nullptr;
    }
}
