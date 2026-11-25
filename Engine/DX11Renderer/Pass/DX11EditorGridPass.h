#pragma once
#include "DX11Renderer/Pipeline/DX11MeshDraw.h"

namespace Canape::EditorGridPass
{
    void Execute(ID3D11DeviceContext& context, const DX11PipelineState& pipelines, ID3D11Buffer* constants,
        const MainTargets& main, RendererDebugStats* stats);
}
