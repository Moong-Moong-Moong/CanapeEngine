#pragma once

#include "Asset/Material/MaterialDesc.h"
#include "Renderer/Handle/TextureHandle.h"

namespace Canape
{
    // Descriptor snapshot only. No per-material GPU resource is needed for BaseColor.
    struct DX11MaterialResource
    {
        MaterialDesc Desc;
        TextureHandle BaseColorTexture;
    };
}
