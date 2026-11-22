#pragma once
#include "DX11Renderer/Pipeline/DX11MeshDraw.h"

namespace Canape::OutlinePass
{
    void DrawMask(const PreparedScene& scene, MeshDrawContext& context,
        ID3D11RenderTargetView* mask, ID3D11DepthStencilView* sceneDepth,
        uint32_t width, uint32_t height, ID3D11Buffer* maskBuffer);
    void Composite(ID3D11DeviceContext& context, const DX11PipelineState& pipelines,
        ID3D11ShaderResourceView* mask, ID3D11RenderTargetView* target,
        uint32_t width, uint32_t height, ID3D11Buffer* constants,
        ID3D11ShaderResourceView* styles, uint32_t styleCount, uint32_t maxThickness, RendererDebugStats* stats,
        bool multisampled = false);
}
