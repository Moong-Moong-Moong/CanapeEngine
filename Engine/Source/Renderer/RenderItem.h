#pragma once

#include "Renderer/Handle/MaterialHandle.h"
#include "Renderer/Handle/MeshHandle.h"
#include "Math/Matrix4.h"

#include <cstdint>

namespace Canape
{
    struct RenderItem
    {
        MeshHandle mesh{};
        uint32_t submeshIndex = 0;
        MaterialHandle material{};
        Matrix4 world{};
        bool castShadow = true;
    };
}
