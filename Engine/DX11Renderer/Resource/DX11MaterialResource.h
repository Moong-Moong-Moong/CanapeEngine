#pragma once

#include "DX11Renderer/Data/MaterialDesc.h"
#include "DX11Renderer/Resource/DX11ResourceId.h"

namespace Canape
{
    // Descriptor snapshot only. No per-material GPU resource is needed for BaseColor.
    struct DX11MaterialResource
    {
        MaterialDesc Desc;
        TextureResourceId BaseColorTexture;
    };
}
