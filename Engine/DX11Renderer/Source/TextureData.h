#pragma once

#include <cstddef>
#include <cstdint>

namespace Canape
{
    // Tightly packed RGBA bytes containing sRGB color. Alpha remains linear.
    enum class TextureFormat { RGBA8_SRGB };

    struct TextureAssetId
    {
        uint64_t Value = 0; // CPU asset identity; zero is invalid.
        constexpr bool IsValid() const { return Value != 0; }
        constexpr bool operator==(const TextureAssetId&) const = default;
    };

    struct TextureHandle
    {
        uint64_t Value = 0; // Renderer registration; nonzero may still be stale.
        constexpr bool IsValid() const { return Value != 0; }
        constexpr bool operator==(const TextureHandle&) const = default;
    };

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
