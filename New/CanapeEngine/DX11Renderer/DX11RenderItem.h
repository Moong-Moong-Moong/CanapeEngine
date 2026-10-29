#pragma once

#include "DX11ResourceId.h"
#include "Math/Matrix4.h"
#include "Math/Vector3.h"

#include <cstdint>

namespace Canape
{
    struct DX11RenderItem
    {
        MeshResourceId mesh;
        uint32_t submeshIndex = 0;
        MaterialResourceId material;
        Matrix4 world;
    };

    struct DX11RenderView
    {
        Matrix4 view;
        Matrix4 projection;
        Vector3 cameraPosition;
    };
}
