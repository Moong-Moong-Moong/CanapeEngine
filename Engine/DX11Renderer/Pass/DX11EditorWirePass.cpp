#include "DX11Renderer/Pass/DX11EditorWirePass.h"
#include <cmath>

namespace Canape::EditorWirePass
{
    void Execute(ID3D11DeviceContext& context, const DX11PipelineState& pipelines,
        ID3D11Buffer* constants, ID3D11Buffer* view, const MainTargets& main,
        std::span<const EditorWireSphere> spheres, RendererDebugStats* stats)
    {
        if (spheres.empty() || !pipelines.EditorWireAvailable() || !constants || !view) return;

        RenderBindings::BindMainTargets(context, main);
        RenderBindings::BindPipeline(context, pipelines.ResolveEditorWire());
        context.IASetInputLayout(nullptr);
        context.IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        context.VSSetConstantBuffers(2, 1, &view);
        constexpr UINT slot = ShaderConstants::EditorWireSlot;
        context.VSSetConstantBuffers(slot, 1, &constants);
        context.PSSetConstantBuffers(slot, 1, &constants);
        for (const auto& sphere : spheres)
        {
            if (!std::isfinite(sphere.radius) || sphere.radius <= 0.0f
                || !std::isfinite(sphere.center.X) || !std::isfinite(sphere.center.Y) || !std::isfinite(sphere.center.Z)
                || !std::isfinite(sphere.color.X) || !std::isfinite(sphere.color.Y)
                || !std::isfinite(sphere.color.Z) || !std::isfinite(sphere.color.W)) continue;
            const ShaderConstants::EditorWireConstants data{ sphere.center, sphere.radius, sphere.color };
            ConstantBuffers::Upload(context, constants, data);
            context.Draw(3 * ShaderConstants::EditorWireSegments * 2, 0);
#if CANAPE_RENDER_DEBUG
            if (stats) ++stats->drawCalls;
#else
            (void)stats;
#endif
        }
        ID3D11Buffer* unbound = nullptr;
        context.VSSetConstantBuffers(slot, 1, &unbound);
        context.PSSetConstantBuffers(slot, 1, &unbound);
        context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
}
