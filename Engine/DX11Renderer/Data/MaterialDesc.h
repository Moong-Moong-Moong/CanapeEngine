#pragma once

#include "Math/Vector4.h"
#include <cstdint>

namespace Canape
{
    enum class AlphaMode { Opaque = 0, Masked = 1, Transparent = 2 };

    struct MaterialDesc
    {
        Vector4 baseColor{ 1, 1, 1, 1 };
        float roughness = 0.5f;
        float metallic = 0.0f;
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
    };
}
