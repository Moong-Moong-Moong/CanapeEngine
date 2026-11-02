#pragma once

#include <cstdint>

namespace Canape
{
    inline constexpr uint32_t ShadowSampleCountOptions[] = { 8, 16, 25, 32, 64, 128 };
    inline constexpr float MinShadowDistance = 5.0f;

    struct ShadowFilterSettings
    {
        bool pcss = true;
        float shadowDistance = 40.0f;
        uint32_t pcfSampleCount = 64;
        uint32_t blockerSampleCount = 25;
        bool legacyPcf25 = true;
        float pcfTapSpacingScale = 1.5f;
        float blockerSearchRadius = 0.4f;
    };
}
