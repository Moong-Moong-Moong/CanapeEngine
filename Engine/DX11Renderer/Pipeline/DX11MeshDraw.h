#pragma once
#include "DX11Renderer/Pipeline/DX11RenderBindings.h"
#include "DX11Renderer/Frame/DX11ScenePreparation.h"
#include "DX11Renderer/Data/RendererDebugStats.h"

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
        ID3D11ShaderResourceView* TestNormalTexture = nullptr;
        const RendererDebugSettings* DebugSettings = nullptr;
        RendererDebugStats* Stats = nullptr;
    };
    namespace MeshDraw
    {
        void SubmitMain(const PreparedMeshDraw& draw, MeshDrawContext& context);
        void SubmitShadow(const PreparedMeshDraw& draw, MeshDrawContext& context);
    }
}
