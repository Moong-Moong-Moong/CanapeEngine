#pragma once
#include "DX11Renderer/Pipeline/DX11RenderBindings.h"
#include "DX11Renderer/Frame/DX11ScenePreparation.h"
#include "DX11Renderer/Data/RendererDebugStats.h"

#include <span>

namespace Canape
{
    // A draw only borrows its immediate context, constants, pipeline and existing diagnostics.
    struct MeshDrawContext
    {
        ID3D11DeviceContext& Context;
        const DX11PipelineState& Pipelines;
        ID3D11Buffer* ObjectBuffer;
        ID3D11Buffer* MaterialBuffer;
        SceneBindings Scene;
        const RendererDebugSettings* DebugSettings = nullptr;
        RendererDebugStats* Stats = nullptr;
        ID3D11Buffer* SkinningBuffer = nullptr;
        std::span<const Matrix4> BonePalettes{}; // Borrowed for this PreparedScene's submission.
    };
    namespace MeshDraw
    {
        void SubmitMain(const PreparedMeshDraw& draw, MeshDrawContext& context,
            TransparentFace face = TransparentFace::Both);
        void SubmitShadow(const PreparedMeshDraw& draw, MeshDrawContext& context);
        void SubmitOutlineMask(const PreparedMeshDraw& draw, MeshDrawContext& context, ID3D11Buffer* maskBuffer);
    }
}
