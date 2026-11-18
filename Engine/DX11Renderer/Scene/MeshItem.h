#pragma once

#include "DX11Renderer/Resource/DX11ResourceId.h"
#include "Math/Matrix4.h"

#include <cstdint>

namespace Canape
{
    struct MeshItem
    {
        MeshResourceId mesh{};
        uint32_t submeshIndex = 0;
        MaterialResourceId material{};
        Matrix4 world{};
        bool castShadow = true;
    };

    struct SkinnedMeshItem
    {
        MeshResourceId mesh{};
        uint32_t submeshIndex = 0;
        MaterialResourceId material{};
        Matrix4 world{};
        uint32_t paletteOffset = 0;
        uint32_t paletteCount = 0;
        bool castShadow = true;
    };
}
