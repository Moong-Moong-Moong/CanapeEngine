#pragma once

#include "Renderer/Handle/MaterialHandle.h"
#include "Renderer/Handle/MeshHandle.h"
#include "Math/Matrix4.h"

#include <cstdint>

namespace Canape
{
    // One submesh draw. Handles must be registered with the renderer that consumes the item.
    struct RenderItem
    {
        MeshHandle mesh{};
        uint32_t submeshIndex = 0;
        MaterialHandle material{};
        Matrix4 world{};
        bool castShadow = true;
    };
}
