#include "DX11Renderer/Pass/DX11FinalOutputPass.h"

namespace Canape::FinalOutputPass
{
    void Execute(const PreparedFinalOutput& output, ID3D11DeviceContext& context,
        ID3D11RenderTargetView* target, uint32_t width, uint32_t height,
        ID3D11Buffer* depthDisplay, ID3D11Buffer* postProcess, RendererDebugStats* stats)
    {
        // Rebinding the output target unbinds SceneColor RTV and camera depth before SRV reads.
        context.OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MaxDepth = 1.0f;
        context.RSSetViewports(1, &viewport);
        RenderBindings::BindPipeline(context, output.Pipeline);
        context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context.PSSetShaderResources(0, 1, &output.Texture);
        if (output.DisplayDepth) context.PSSetConstantBuffers(0, 1, &depthDisplay);
        context.PSSetConstantBuffers(ShaderConstants::PostProcessSlot, 1, &postProcess);
        context.Draw(3, 0);
#if CANAPE_RENDER_DEBUG
        ++stats->drawCalls;
        ++stats->triangleCount;
#else
        (void)stats;
#endif
        ID3D11ShaderResourceView* unbound = nullptr;
        context.PSSetShaderResources(0, 1, &unbound);
        ID3D11Buffer* unboundBuffer = nullptr;
        context.PSSetConstantBuffers(0, 1, &unboundBuffer);
        context.PSSetConstantBuffers(ShaderConstants::PostProcessSlot, 1, &unboundBuffer);
    }
}
