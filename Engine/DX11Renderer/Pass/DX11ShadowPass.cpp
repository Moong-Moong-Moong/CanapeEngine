#include "DX11Renderer/Pass/DX11ShadowPass.h"

namespace Canape::ShadowPass
{
    void Execute(const PreparedScene& scene, MeshDrawContext& context,
        const Target& shadow, const MainTargets& main)
    {
        auto& gpu = context.Context;
        // The shadow map must stop being an SRV before it becomes a DSV.
        ID3D11ShaderResourceView* unbound = nullptr;
        gpu.PSSetShaderResources(1, 1, &unbound);
        gpu.OMSetRenderTargets(0, nullptr, shadow.Depth);
        gpu.ClearDepthStencilView(shadow.Depth, D3D11_CLEAR_DEPTH, 1.0f, 0);
        D3D11_VIEWPORT viewport{};
        viewport.Width = viewport.Height = static_cast<float>(shadow.Resolution);
        viewport.MaxDepth = 1.0f;
        gpu.RSSetViewports(1, &viewport);
        gpu.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* shadowBuffer = context.Scene.Constants.Shadows;
        gpu.VSSetConstantBuffers(5, 1, &shadowBuffer);
        for (size_t index : scene.ShadowCasters) MeshDraw::SubmitShadow(scene.Draws[index], context);
        RenderBindings::BindMainTargets(gpu, main);
        RenderBindings::BindPipeline(gpu, context.Pipelines.Resolve(VertexFormat::Position, AlphaMode::Opaque));
    }
}
