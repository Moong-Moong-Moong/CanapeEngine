#include "DX11Renderer/Pass/DX11SkyboxPass.h"

namespace Canape::SkyboxPass
{
    void Execute(const PreparedSkybox& skybox, ID3D11DeviceContext& context,
        const DX11PipelineState& pipelines, const Geometry& geometry, const MainTargets& main,
        RendererDebugStats* stats)
    {
        if (!skybox.Texture || !geometry.Vertices || !geometry.Indices || !geometry.Constants) return;
        RenderBindings::BindMainTargets(context, main);
        const auto pipeline = pipelines.ResolveSkybox();
        RenderBindings::BindPipeline(context, pipeline);
        constexpr UINT stride = sizeof(float) * 2;
        constexpr UINT offset = 0;
        context.IASetVertexBuffers(0, 1, &geometry.Vertices, &stride, &offset);
        context.IASetIndexBuffer(geometry.Indices, DXGI_FORMAT_R32_UINT, 0);
        context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context.VSSetConstantBuffers(6, 1, &geometry.Constants);
        context.PSSetConstantBuffers(6, 1, &geometry.Constants);
        context.PSSetShaderResources(2, 1, &skybox.Texture);
        context.PSSetSamplers(2, 1, &pipeline.Sampler);
        context.DrawIndexed(6, 0, 0);
#if CANAPE_RENDER_DEBUG
        ++stats->drawCalls;
        stats->triangleCount += 2;
#else
        (void)stats;
#endif
        ID3D11ShaderResourceView* unboundView = nullptr;
        ID3D11SamplerState* unboundSampler = nullptr;
        ID3D11Buffer* unboundBuffer = nullptr;
        context.PSSetShaderResources(2, 1, &unboundView);
        context.PSSetSamplers(2, 1, &unboundSampler);
        context.VSSetConstantBuffers(6, 1, &unboundBuffer);
        context.PSSetConstantBuffers(6, 1, &unboundBuffer);
    }
}
