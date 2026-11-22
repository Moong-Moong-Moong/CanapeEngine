#pragma once

#include <cstdint>

#include "Math/MathUtility.h"
#include "Scene/Component/Component.h"

namespace Canape
{
    // The terrain starts at the world origin; the Transform is ignored.
    class TerrainGeneratorComponent final: public Component
    {
        RTTR_ENABLE(Component)

    private:
        std::uint32_t seed = 1;
        // Meters: the heights stay between the base and this much above it.
        float amount = 520.0f;

    public:
        std::uint32_t GetSeed() const
        {
            return seed;
        }
        void SetSeed(std::uint32_t value)
        {
            seed = value;
        }

        float GetAmount() const
        {
            return amount;
        }
        void SetAmount(float meters)
        {
            amount = Math::Max(meters, 0.0f);
        }
    };
}
