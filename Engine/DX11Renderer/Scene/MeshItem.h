#pragma once

#include "DX11Renderer/Resource/DX11ResourceId.h"
#include "Math/Matrix4.h"
#include "DX11Renderer/Data/OutlineStyle.h"

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
        uint64_t objectId = 0; // Component identity, shared by all its submeshes. Zero is invalid.
        bool outlineEnabled = false;
        OutlineStyle outlineStyle{};
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
        uint64_t objectId = 0;
        bool outlineEnabled = false;
        OutlineStyle outlineStyle{};
    };
}
