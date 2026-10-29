#pragma once

#include "DX11ResourceId.h"
#include "Math/Vector4.h"

#include <cstdint>

namespace Canape
{
    enum class DX11AlphaMode : uint32_t { Opaque = 0, Masked = 1, Transparent = 2 };

    struct DX11MaterialDesc
    {
        Vector4 baseColor{ 1, 1, 1, 1 };
        float roughness = 0.5f;
        float metallic = 0.0f;
        DX11AlphaMode alphaMode = DX11AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
    };

    struct DX11MaterialResource
    {
        MaterialResourceId Id;

        DX11MaterialDesc Desc;
        TextureResourceId BaseColorTexture;
    };
}
