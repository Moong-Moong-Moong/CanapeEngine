#pragma once
#include "DX11Renderer/Pipeline/DX11MeshDraw.h"
#include <span>

namespace Canape::EditorWirePass
{
    void Execute(ID3D11DeviceContext& context, const DX11PipelineState& pipelines,
        ID3D11Buffer* constants, ID3D11Buffer* view, const MainTargets& main,
        std::span<const EditorWireSphere> spheres, RendererDebugStats* stats);
}
