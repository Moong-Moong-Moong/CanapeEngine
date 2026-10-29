#pragma once

#include "DX11Renderer/Resource/DX11ResourceId.h"
#include "Math/Matrix4.h"

#include <cstdint>

namespace Canape
{
    struct RenderItem
    {
        MeshResourceId mesh{};
        uint32_t submeshIndex = 0;
        MaterialResourceId material{};
        Matrix4 world{};
        bool castShadow = true;
    };
}
