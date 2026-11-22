#pragma once
#include "DX11Renderer/Data/RendererDebugStats.h"
#include "DX11Renderer/Data/UIDrawList.h"
#include "DX11Renderer/Pipeline/DX11PipelineState.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <string>

namespace Canape
{
    class DX11ResourceCache;

    // Owned by the renderer Impl. The pass grows the dynamic buffers on demand.
    struct DX11UIBuffers
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> Vertices;
        Microsoft::WRL::ComPtr<ID3D11Buffer> Indices;
        Microsoft::WRL::ComPtr<ID3D11Buffer> Constants;
        uint32_t VertexCapacity = 0;
        uint32_t IndexCapacity = 0;
    };

    namespace UIPass
    {
        // Draws the list into target (expected to be a UNORM view: blending stays in sRGB space).
        // sceneDepth: the camera depth texture (same pixel size as target; Texture2DMS when the pipeline
        // was resolved for MSAA) for world-space commands, or null to draw them untested.
        // Leaves t0/t1/b0 unbound. Returns false only on GPU resource failure.
        bool Execute(const UIDrawList& list, const DX11PipelineSelection& pipeline,
            const DX11ResourceCache& resources, DX11UIBuffers& buffers,
            ID3D11Device& device, ID3D11DeviceContext& context,
            ID3D11RenderTargetView* target, ID3D11ShaderResourceView* sceneDepth, uint32_t width, uint32_t height,
            RendererDebugStats* stats, std::string& error);
    }
}
