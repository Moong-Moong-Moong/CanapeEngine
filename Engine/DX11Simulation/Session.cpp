#include "DX11Simulation/Session.h"

#include <utility>

#include "DX11Simulation/Device.h"

namespace Canape
{
    Session::~Session() = default;

    Session::Session(ID3D11Device& device, ID3D11DeviceContext& context)
        : device(std::make_unique<Device>(device, context)) {}

    BufferHandle Session::CreateBuffer(
        const D3D11_BUFFER_DESC& desc,
        const D3D11_SUBRESOURCE_DATA* initialData,
        std::string_view name)
    {
        auto buffer = device->CreateBuffer(desc, initialData, name);
        return buffers.Add(std::move(buffer));
    }

    TextureHandle Session::CreateTexture(
        const D3D11_TEXTURE2D_DESC& desc,
        const D3D11_SUBRESOURCE_DATA* initialData,
        std::string_view name)
    {
        auto texture = device->CreateTexture(desc, initialData, name);
        return textures.Add(std::move(texture));
    }

    SamplerStateHandle Session::GetOrCreateSamplerState(const D3D11_SAMPLER_DESC& desc)
    {
        return samplerStates.GetOrCreate(
            desc,
            [&] { return device->CreateSamplerState(desc); }
        );
    }

    RasterizerStateHandle Session::GetOrCreateRasterizerState(const D3D11_RASTERIZER_DESC& desc)
    {
        return rasterizerStates.GetOrCreate(
            desc,
            [&] { return device->CreateRasterizerState(desc); }
        );
    }

    DepthStencilStateHandle Session::GetOrCreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& desc)
    {
        return depthStencilStates.GetOrCreate(
            desc,
            [&] { return device->CreateDepthStencilState(desc); }
        );
    }

    BlendStateHandle Session::GetOrCreateBlendState(const D3D11_BLEND_DESC& desc)
    {
        return blendStates.GetOrCreate(
            desc,
            [&] { return device->CreateBlendState(desc); }
        );
    }

    VertexShaderHandle Session::GetOrCreateVertexShader(const ShaderDesc& desc)
    {
        return vertexShaders.GetOrCreate(
            desc,
            [&] { return device->CreateVertexShader(desc); }
        );
    }

    PixelShaderHandle Session::GetOrCreatePixelShader(const ShaderDesc& desc)
    {
        return pixelShaders.GetOrCreate(
            desc,
            [&] { return device->CreatePixelShader(desc); }
        );
    }

    ComputeShaderHandle Session::GetOrCreateComputeShader(const ShaderDesc& desc)
    {
        return computeShaders.GetOrCreate(
            desc,
            [&] { return device->CreateComputeShader(desc); }
        );
    }
}
