#pragma once
#include "DX11Renderer/Pipeline/DX11MeshDraw.h"

namespace Canape::FinalOutputPass
{
    void Execute(const PreparedFinalOutput& output, ID3D11DeviceContext& context,
        ID3D11RenderTargetView* target, uint32_t width, uint32_t height,
        ID3D11Buffer* depthDisplay, RendererDebugStats* stats);
}
