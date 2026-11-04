#pragma once
#include "DX11Renderer/Pipeline/DX11MeshDraw.h"

namespace Canape::SkyboxPass
{
    struct Geometry
    {
        ID3D11Buffer* Vertices;
        ID3D11Buffer* Indices;
        ID3D11Buffer* Constants;
    };
    void Execute(const PreparedSkybox& skybox, ID3D11DeviceContext& context,
        const DX11PipelineState& pipelines, const Geometry& geometry, const MainTargets& main,
        RendererDebugStats* stats);
}
