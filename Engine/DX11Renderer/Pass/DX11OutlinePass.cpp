#include "DX11Renderer/Pass/DX11OutlinePass.h"

namespace Canape::OutlinePass
{
    void DrawMask(const PreparedScene& scene, MeshDrawContext& context,
        ID3D11RenderTargetView* mask, ID3D11DepthStencilView* sceneDepth,
        uint32_t width, uint32_t height, ID3D11Buffer* maskBuffer)
    {
        auto& gpu = context.Context;
        ID3D11ShaderResourceView* unbound = nullptr;
        gpu.PSSetShaderResources(0, 1, &unbound);
        const float background[4]{};
        gpu.ClearRenderTargetView(mask, background); // Background ID = 0.
        RenderBindings::BindMainTargets(gpu, { mask, sceneDepth, width, height });
        ID3D11Buffer* view = context.Scene.Constants.View;
        gpu.VSSetConstantBuffers(2, 1, &view);
        for (size_t index : scene.OutlineDraws)
            MeshDraw::SubmitOutlineMask(scene.Draws[index], context, maskBuffer);
        gpu.OMSetRenderTargets(0, nullptr, nullptr);
        gpu.PSSetShaderResources(0, 1, &unbound);
        ID3D11Buffer* unboundBuffer = nullptr;
        gpu.PSSetConstantBuffers(ShaderConstants::OutlineSlot, 1, &unboundBuffer);
    }

    void Composite(ID3D11DeviceContext& context, const DX11PipelineState& pipelines,
        ID3D11ShaderResourceView* mask, ID3D11RenderTargetView* target,
        uint32_t width, uint32_t height, ID3D11Buffer* constants,
        ID3D11ShaderResourceView* styles, uint32_t styleCount, uint32_t maxThickness, RendererDebugStats* stats, bool multisampled)
    {
        RenderBindings::BindMainTargets(context, { target, nullptr, width, height });
        RenderBindings::BindPipeline(context, pipelines.ResolveOutlineComposite(multisampled));
        ShaderConstants::OutlineCompositeConstants data{};
        data.StyleCount = styleCount;
        data.MaxThickness = maxThickness;
        data.OutputWidth = static_cast<float>(width);
        data.OutputHeight = static_cast<float>(height);
        ConstantBuffers::Upload(context, constants, data);
        context.PSSetConstantBuffers(ShaderConstants::OutlineSlot, 1, &constants);
        context.PSSetShaderResources(0, 1, &mask);
        context.PSSetShaderResources(ShaderConstants::OutlineStylesSlot, 1, &styles);
        context.Draw(3, 0);
#if CANAPE_RENDER_DEBUG
        ++stats->drawCalls;
        ++stats->triangleCount;
#else
        (void)stats;
#endif
        ID3D11ShaderResourceView* unbound = nullptr;
        context.PSSetShaderResources(0, 1, &unbound);
        context.PSSetShaderResources(ShaderConstants::OutlineStylesSlot, 1, &unbound);
        ID3D11Buffer* unboundBuffer = nullptr;
        context.PSSetConstantBuffers(ShaderConstants::OutlineSlot, 1, &unboundBuffer);
    }
}
