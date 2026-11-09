#include "DX11Renderer/Pipeline/DX11MeshDraw.h"

namespace Canape::MeshDraw
{
    namespace
    {
        void UploadAndBindObject(const PreparedMeshDraw& draw, MeshDrawContext& context)
        {
            ConstantBuffers::Upload(context.Context, context.ObjectBuffer, draw.Object);
            RenderBindings::BindObject(context.Context, context.ObjectBuffer);
        }
        void UploadAndBindMaterial(const PreparedMeshDraw& draw, MeshDrawContext& context)
        {
            ConstantBuffers::Upload(context.Context, context.MaterialBuffer, draw.Material.Constants);
            RenderBindings::BindMaterial(context.Context, context.MaterialBuffer, draw.Material.Texture);
        }
        void SubmitGeometry(const PreparedMeshDraw& draw, MeshDrawContext& context)
        {
            RenderBindings::BindGeometry(context.Context, *draw.Mesh);
            const SubMesh& part = draw.Mesh->SubMeshes[draw.SubmeshIndex];
            context.Context.DrawIndexed(part.indexCount, part.startIndex, part.baseVertex);
#if CANAPE_RENDER_DEBUG
            ++context.Stats->drawCalls;
            context.Stats->triangleCount += part.indexCount / 3;
#endif
        }
    }

    void SubmitMain(const PreparedMeshDraw& draw, MeshDrawContext& context)
    {
        UploadAndBindObject(draw, context);
        UploadAndBindMaterial(draw, context);
        context.Context.PSSetShaderResources(4, 1, &draw.Material.ORMTexture);
        // Temporary renderer-wide normal-map binding until material routing is available.
        context.Context.PSSetShaderResources(3, 1, &context.TestNormalTexture);
        const AlphaMode alpha = draw.Material.Description.alphaMode;
#if CANAPE_RENDER_DEBUG
        const auto& settings = *context.DebugSettings;
        const DebugViewMode view = settings.enabled ? settings.viewMode : DebugViewMode::Final;
        const bool wireframe = settings.enabled && settings.wireframe;
        const auto pipeline = context.Pipelines.Resolve(draw.Mesh->Format, alpha, view, wireframe);
#else
        const auto pipeline = context.Pipelines.Resolve(draw.Mesh->Format, alpha);
#endif
        RenderBindings::BindPipeline(context.Context, pipeline);
        RenderBindings::BindScene(context.Context, context.Scene);
        SubmitGeometry(draw, context);
    }

    void SubmitShadow(const PreparedMeshDraw& draw, MeshDrawContext& context)
    {
        const AlphaMode alpha = draw.Material.Description.alphaMode;
        RenderBindings::BindPipeline(context.Context, context.Pipelines.ResolveShadow(draw.Mesh->Format, alpha));
        UploadAndBindObject(draw, context);
        if (alpha == AlphaMode::Masked) UploadAndBindMaterial(draw, context);
        SubmitGeometry(draw, context);
    }
}
