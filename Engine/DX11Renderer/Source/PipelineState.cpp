#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "PipelineState.h"
#include "ShaderCompiler.h"

#include <iterator>
#include <sstream>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace Canape
{
    namespace
    {
        // Match the engine's row-major matrix storage; CPU uploads are not transposed.
        constexpr UINT kShaderCompileFlags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

        bool Fail(std::string& error, const char* operation, HRESULT result)
        {
            std::ostringstream stream;
            stream << operation << " failed (HRESULT 0x" << std::hex
                   << static_cast<unsigned long>(result) << ')';
            error = stream.str();
            return false;
        }
    }

    bool PipelineState::Initialize(ID3D11Device* device, std::string& error)
    {
        *this = PipelineState{};
        error.clear();
        if (!device)
        {
            error = "PipelineState::Initialize requires a renderer device";
            return false;
        }
        // Publish only a complete set. Any failed creation releases temporary objects.
        PipelineState pending;
        ComPtr<ID3DBlob> vertexBytecode;
        ComPtr<ID3DBlob> pixelBytecode;
        if (!CompileShaderFile(L"Shaders/Mesh.hlsl", "VSMain", "vs_5_0", nullptr,
            kShaderCompileFlags, vertexBytecode, error)
            || !CompileShaderFile(L"Shaders/Mesh.hlsl", "PSMain", "ps_5_0", nullptr,
                kShaderCompileFlags, pixelBytecode, error))
            return false;

        HRESULT result = device->CreateVertexShader(
            vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
            nullptr, &pending.VertexShader);
        if (FAILED(result))
        {
            return Fail(error, "ID3D11Device::CreateVertexShader", result);
        }

        result = device->CreatePixelShader(
            pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
            nullptr, &pending.PixelShader);
        if (FAILED(result))
        {
            return Fail(error, "ID3D11Device::CreatePixelShader", result);
        }

        constexpr D3D11_INPUT_ELEMENT_DESC inputElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
              D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        result = device->CreateInputLayout(
            inputElements, static_cast<UINT>(std::size(inputElements)),
            vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
            &pending.InputLayout);
        if (FAILED(result))
        {
            return Fail(error, "ID3D11Device::CreateInputLayout", result);
        }

        // Two fixed vertex formats, each compiled against its actual input signature.
        const D3D_SHADER_MACRO staticDefines[] = { { "STATIC_VERTEX", "1" }, { nullptr, nullptr } };
        if (!CompileShaderFile(L"Shaders/Mesh.hlsl", "VSMain", "vs_5_0", staticDefines,
            kShaderCompileFlags, vertexBytecode, error))
            return false;
        result = device->CreateVertexShader(vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
            nullptr, &pending.StaticVertexShader);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateVertexShader(static)", result);

        const D3D11_INPUT_ELEMENT_DESC staticElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(StaticVertex, Position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(StaticVertex, Normal)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(StaticVertex, UV)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        result = device->CreateInputLayout(staticElements, static_cast<UINT>(std::size(staticElements)),
            vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(), &pending.StaticInputLayout);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateInputLayout(static)", result);

        D3D11_BLEND_DESC blendDesc{};
        auto& blend = blendDesc.RenderTarget[0];
        blend.SrcBlend = blend.SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.DestBlend = blend.DestBlendAlpha = D3D11_BLEND_ZERO;
        blend.BlendOp = blend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        result = device->CreateBlendState(&blendDesc, &pending.OpaqueBlendState);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateBlendState(opaque)", result);
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        // Straight-alpha RGB; accumulated alpha is As + Ad * (1 - As).
        blend.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        result = device->CreateBlendState(&blendDesc, &pending.TransparentBlendState);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateBlendState(transparent)", result);

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
        depthDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
        depthDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
        depthDesc.FrontFace = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS };
        depthDesc.BackFace = depthDesc.FrontFace;
        result = device->CreateDepthStencilState(&depthDesc, &pending.OpaqueDepthState);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateDepthStencilState(opaque)", result);
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        result = device->CreateDepthStencilState(&depthDesc, &pending.TransparentDepthState);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateDepthStencilState(transparent)", result);

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        result = device->CreateSamplerState(&samplerDesc, &pending.LinearSampler);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateSamplerState(linear wrap)", result);

        D3D11_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = D3D11_CULL_NONE;
        rasterizerDesc.DepthClipEnable = TRUE;
        result = device->CreateRasterizerState(&rasterizerDesc, &pending.RasterizerState);
        if (FAILED(result))
        {
            return Fail(error, "ID3D11Device::CreateRasterizerState", result);
        }


        *this = std::move(pending);
        return true;
    }

    PipelineSelection PipelineState::Resolve(VertexFormat vertexFormat, AlphaMode alphaMode) const
    {
        const bool isStatic = vertexFormat == VertexFormat::Static;
        const bool transparent = alphaMode == AlphaMode::Transparent;
        return {
            isStatic ? StaticVertexShader.Get() : VertexShader.Get(),
            PixelShader.Get(),
            isStatic ? StaticInputLayout.Get() : InputLayout.Get(),
            RasterizerState.Get(),
            transparent ? TransparentBlendState.Get() : OpaqueBlendState.Get(),
            transparent ? TransparentDepthState.Get() : OpaqueDepthState.Get(),
            LinearSampler.Get()
        };
    }
}
