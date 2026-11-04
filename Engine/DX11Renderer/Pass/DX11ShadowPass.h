#pragma once
#include "DX11Renderer/Pipeline/DX11MeshDraw.h"

namespace Canape::ShadowPass
{
    struct Target
    {
        ID3D11DepthStencilView* Depth;
        uint32_t Resolution;
    };
    void Execute(const PreparedScene& scene, MeshDrawContext& context,
        const Target& shadow, const MainTargets& main);
}
