#pragma once

#include <cstdint>

#include "Math/MathUtility.h"
#include "Math/Vector3.h"
#include "Scene/Component/Component.h"

namespace Canape
{
    // The box in the object's space the terrain is generated in; heights are ratios of its height.
    inline constexpr Vector3 TerrainBoxMin { -0.5f, 0.0f, -0.5f };
    inline constexpr Vector3 TerrainBoxMax { 0.5f, 1.0f, 0.5f };

    class TerrainGeneratorComponent final: public Component
    {
        RTTR_ENABLE(Component)

    private:
        std::uint32_t seed = 1;
        float baseHeightRatio = 0.01f;
        float maxHeightRatio = 0.18f;

    public:
        std::uint32_t GetSeed() const
        {
            return seed;
        }
        void SetSeed(std::uint32_t value)
        {
            seed = value;
        }

        float GetBaseHeightRatio() const
        {
            return baseHeightRatio;
        }
        void SetBaseHeightRatio(float ratio)
        {
            baseHeightRatio = Math::Min(Math::Max(ratio, 0.0f), 1.0f);
            maxHeightRatio = Math::Max(maxHeightRatio, baseHeightRatio);
        }

        float GetMaxHeightRatio() const
        {
            return maxHeightRatio;
        }
        void SetMaxHeightRatio(float ratio)
        {
            maxHeightRatio = Math::Min(Math::Max(ratio, 0.0f), 1.0f);
            baseHeightRatio = Math::Min(baseHeightRatio, maxHeightRatio);
        }
    };
}
