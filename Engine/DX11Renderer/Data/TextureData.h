#pragma once

#include <cstddef>
#include <cstdint>

namespace Canape
{
    // Tightly packed RGBA bytes containing sRGB color. Alpha remains linear.
    enum class TextureFormat { RGBA8_SRGB };

    inline const char* ValidateTextureData(uint32_t width, uint32_t height, TextureFormat format, size_t byteCount)
    {
        if (format != TextureFormat::RGBA8_SRGB) return "Unsupported texture pixel format";
        if (width == 0 || height == 0) return "Texture dimensions must be nonzero";
        const uint64_t pixels = uint64_t(width) * height;
        if (pixels > SIZE_MAX / 4 || byteCount != pixels * 4)
            return "Texture requires tightly packed width * height * 4 RGBA bytes";
        return nullptr;
    }
}
