#pragma once

#include "MaterialHandle.h"
#include "MeshHandle.h"
#include "Math/Matrix4.h"
#include "Math/Vector3.h"

#include <cstdint>
#include <vector>

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

    struct RenderView
    {
        Matrix4 view{};
        Matrix4 projection{};
        Vector3 cameraPosition{};
    };

    // A synchronous snapshot. Referenced GPU meshes must live until DrawFrame returns.
    struct RenderFrame
    {
        RenderView camera;
        std::vector<RenderItem> items;
    };
}
