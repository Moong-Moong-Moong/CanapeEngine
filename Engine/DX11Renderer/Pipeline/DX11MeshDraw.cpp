#include "DX11Renderer/Pipeline/DX11MeshDraw.h"

#include <algorithm>
#include <cassert>

namespace Canape::MeshDraw
{
    namespace
    {
        void UploadAndBindSkinning(const PreparedMeshDraw& draw, MeshDrawContext& context)
        {
            if (draw.Mesh->Format != VertexFormat::Skinned)
            {
                RenderBindings::BindSkinning(context.Context, nullptr);
                return;
            }

            assert(context.SkinningBuffer);
            assert(draw.PaletteCount > 0 && draw.PaletteCount <= ShaderConstants::MaxSkinMatrices);
            assert(draw.PaletteOffset <= context.BonePalettes.size()
                && draw.PaletteCount <= context.BonePalettes.size() - draw.PaletteOffset);

            // UpdateSubresource uploads the entire constant buffer. Copy into a full
            // palette so short draws never read beyond their CPU source allocation.
            ShaderConstants::SkinningConstants constants{};
            std::copy_n(context.BonePalettes.begin() + draw.PaletteOffset,
                draw.PaletteCount, constants.SkinMatrices);
            ConstantBuffers::Upload(context.Context, context.SkinningBuffer, constants);
            RenderBindings::BindSkinning(context.Context, context.SkinningBuffer);
        }

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

    void SubmitMain(const PreparedMeshDraw& draw, MeshDrawContext& context, TransparentFace face)
    {
        UploadAndBindSkinning(draw, context);
        UploadAndBindObject(draw, context);
        UploadAndBindMaterial(draw, context);
        context.Context.PSSetShaderResources(4, 1, &draw.Material.ORMTexture);
        // Bind null views too, so a material cannot inherit the previous draw's maps.
        context.Context.PSSetShaderResources(3, 1, &draw.Material.NormalTexture);
        context.Context.PSSetShaderResources(5, 1, &draw.Material.OcclusionTexture);
        context.Context.PSSetShaderResources(ShaderConstants::LightmapTextureSlot, 1, &draw.Material.LightmapTexture);
        const AlphaMode alpha = draw.Material.Description.alphaMode;
#if CANAPE_RENDER_DEBUG
        const auto& settings = *context.DebugSettings;
        const DebugViewMode view = settings.enabled ? settings.viewMode : DebugViewMode::Final;
        const bool wireframe = settings.enabled && settings.wireframe;
        const auto pipeline = context.Pipelines.Resolve(draw.Mesh->Format, alpha, view, wireframe, face);
#else
        const auto pipeline = context.Pipelines.Resolve(draw.Mesh->Format, alpha,
            DebugViewMode::Final, false, face);
#endif
        RenderBindings::BindPipeline(context.Context, pipeline);
        RenderBindings::BindScene(context.Context, context.Scene);
        SubmitGeometry(draw, context);
    }

    void SubmitOutlineMask(const PreparedMeshDraw& draw, MeshDrawContext& context, ID3D11Buffer* maskBuffer)
    {
        UploadAndBindSkinning(draw, context);
        UploadAndBindObject(draw, context);
        UploadAndBindMaterial(draw, context);
        ShaderConstants::OutlineMaskConstants constants{};
        constants.MaskId = draw.OutlineMaskId;
        ConstantBuffers::Upload(context.Context, maskBuffer, constants);
        context.Context.PSSetConstantBuffers(ShaderConstants::OutlineSlot, 1, &maskBuffer);
        RenderBindings::BindPipeline(context.Context, context.Pipelines.ResolveOutlineMask(draw.Mesh->Format));
        SubmitGeometry(draw, context);
    }

    void SubmitPickId(const PreparedMeshDraw& draw, MeshDrawContext& context, ID3D11Buffer* maskBuffer, uint32_t pickId)
    {
        UploadAndBindSkinning(draw, context);
        UploadAndBindObject(draw, context);
        UploadAndBindMaterial(draw, context);
        ShaderConstants::OutlineMaskConstants constants{};
        constants.MaskId = pickId;
        ConstantBuffers::Upload(context.Context, maskBuffer, constants);
        context.Context.PSSetConstantBuffers(ShaderConstants::OutlineSlot, 1, &maskBuffer);
        RenderBindings::BindPipeline(context.Context, context.Pipelines.ResolvePickId(draw.Mesh->Format));
        SubmitGeometry(draw, context);
    }

    void SubmitShadow(const PreparedMeshDraw& draw, MeshDrawContext& context)
    {
        UploadAndBindSkinning(draw, context);
        const AlphaMode alpha = draw.Material.Description.alphaMode;
        RenderBindings::BindPipeline(context.Context, context.Pipelines.ResolveShadow(draw.Mesh->Format, alpha));
        UploadAndBindObject(draw, context);
        if (alpha == AlphaMode::Masked) UploadAndBindMaterial(draw, context);
        SubmitGeometry(draw, context);
    }
}
