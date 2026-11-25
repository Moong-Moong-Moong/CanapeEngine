#include "DX11Renderer/Pass/DX11EditorGridPass.h"
#include "DX11Renderer/Data/ShaderConstants.h"

namespace Canape::EditorGridPass
{
    void Execute(ID3D11DeviceContext& context, const DX11PipelineState& pipelines, ID3D11Buffer* constants,
        const MainTargets& main, RendererDebugStats* stats)
    {
        if (!pipelines.EditorGridAvailable() || !constants) return;

        RenderBindings::BindMainTargets(context, main);
        RenderBindings::BindPipeline(context, pipelines.ResolveEditorGrid());
        context.IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
        context.IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        constexpr UINT slot = ShaderConstants::EditorGridSlot;
        context.VSSetConstantBuffers(slot, 1, &constants);
        context.PSSetConstantBuffers(slot, 1, &constants);
        context.Draw(6, 0);
#if CANAPE_RENDER_DEBUG
        ++stats->drawCalls;
        stats->triangleCount += 2;
#else
        (void)stats;
#endif
        ID3D11Buffer* unboundBuffer = nullptr;
        context.VSSetConstantBuffers(slot, 1, &unboundBuffer);
        context.PSSetConstantBuffers(slot, 1, &unboundBuffer);
    }
}
