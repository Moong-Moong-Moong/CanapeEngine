#pragma once

#include "Math/Vector3.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Canape
{
    inline constexpr uint32_t MaxOutlineThickness = 16;

    // Submission data and PS structured-buffer element share this 32-byte layout.
    // Keep in sync with Shader/Common/OutlineCompositeCommon.hlsli.
    struct OutlineStyle
    {
        Vector3 Color{ 1.0f, 0.8f, 0.1f }; // Linear SDR, after tone mapping.
        uint32_t Thickness = 2;
        float Opacity = 1.0f;
        float Padding[3]{};
    };
    static_assert(sizeof(OutlineStyle) == 32);
    static_assert(offsetof(OutlineStyle, Thickness) == 12);
    static_assert(offsetof(OutlineStyle, Opacity) == 16);

    inline OutlineStyle SanitizeOutlineStyle(const OutlineStyle& input)
    {
        const OutlineStyle defaults;
        auto channel = [](float value, float fallback)
        {
            return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
        };
        OutlineStyle result;
        result.Color = { channel(input.Color.X, defaults.Color.X), channel(input.Color.Y, defaults.Color.Y),
            channel(input.Color.Z, defaults.Color.Z) };
        result.Thickness = std::clamp(input.Thickness, 1u, MaxOutlineThickness);
        result.Opacity = channel(input.Opacity, defaults.Opacity);
        return result;
    }
}
