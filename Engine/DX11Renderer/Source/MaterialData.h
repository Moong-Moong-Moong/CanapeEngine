#pragma once

#include "Math/Vector4.h"
#include <cstdint>

namespace Canape
{
    // Values also match the fixed pixel shader's MaterialBuffer mode.
    enum class AlphaMode { Opaque = 0, Masked = 1, Transparent = 2 };

    struct MaterialDesc
    {
        // Linear RGB multiplier and linear alpha. No implicit color-space conversion.
        // Future sRGB UI/color-picker input must be decoded once before assigning RGB here.
        Vector4 baseColor{ 1, 1, 1, 1 };
        float roughness = 0.5f;
        float metallic = 0.0f;
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f; // Masked: discard when BaseColor.a * Texture.a is below this.
    };

    // CPU asset identity, distinct from the renderer's MaterialHandle. Zero is invalid.
    struct MaterialAssetId
    {
        uint64_t Value = 0;
        constexpr bool IsValid() const { return Value != 0; }
        constexpr bool operator==(const MaterialAssetId&) const = default;
    };
}
