#pragma once

#include "MeshData.h"
#include "MaterialData.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <string>

namespace Canape
{
    // Borrowed pointers owned by PipelineState. Resolve and bind within the current draw;
    // do not retain selections across initialization or renderer shutdown.
    struct PipelineSelection
    {
        ID3D11VertexShader* VS = nullptr;
        ID3D11PixelShader* PS = nullptr;
        ID3D11InputLayout* InputLayout = nullptr;
        ID3D11RasterizerState* Rasterizer = nullptr;
        ID3D11BlendState* Blend = nullptr;
        ID3D11DepthStencilState* Depth = nullptr;
        ID3D11SamplerState* Sampler = nullptr;
    };

    // The two fixed vertex formats and three existing alpha modes share these objects.
    // No Device/Context ownership, resource registry, or frame execution lives here.
    class PipelineState
    {
    public:
        PipelineState() = default;
        PipelineState(const PipelineState&) = delete;
        PipelineState& operator=(const PipelineState&) = delete;
        PipelineState(PipelineState&&) = default;
        PipelineState& operator=(PipelineState&&) = default;

        bool Initialize(ID3D11Device* device, std::string& error);
        PipelineSelection Resolve(VertexFormat vertexFormat, AlphaMode alphaMode) const;

    private:
        Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> StaticVertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> StaticInputLayout;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> RasterizerState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> OpaqueBlendState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> TransparentBlendState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> OpaqueDepthState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> TransparentDepthState;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> LinearSampler;
    };
}
