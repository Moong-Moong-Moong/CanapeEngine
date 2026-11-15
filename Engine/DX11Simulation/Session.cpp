#include "DX11Simulation/Session.h"

#include <utility>

#include "DX11Simulation/Device.h"

namespace Canape
{
    namespace
    {
        template<typename Handles, typename Key, typename Create>
        auto getOrCreate(Handles& handles, const Key& key, Create&& create)
        {
            if(const auto kept = handles.find(key); kept != handles.end())
            {
                return kept->second;
            }

            const auto handle = create();
            handles.emplace(key, handle);

            return handle;
        }
    }

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
        return getOrCreate(
            samplerStateHandles,
            desc,
            [&] { return samplerStates.Add(device->CreateSamplerState(desc)); }
        );
    }

    RasterizerStateHandle Session::GetOrCreateRasterizerState(const D3D11_RASTERIZER_DESC& desc)
    {
        return getOrCreate(
            rasterizerStateHandles,
            desc,
            [&] { return rasterizerStates.Add(device->CreateRasterizerState(desc)); }
        );
    }

    DepthStencilStateHandle Session::GetOrCreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& desc)
    {
        return getOrCreate(
            depthStencilStateHandles,
            desc,
            [&] { return depthStencilStates.Add(device->CreateDepthStencilState(desc)); }
        );
    }

    BlendStateHandle Session::GetOrCreateBlendState(const D3D11_BLEND_DESC& desc)
    {
        return getOrCreate(
            blendStateHandles,
            desc,
            [&] { return blendStates.Add(device->CreateBlendState(desc)); }
        );
    }

    VertexShaderHandle Session::GetOrCreateVertexShader(const ShaderDesc& desc)
    {
        return getOrCreate(
            vertexShaderHandles,
            desc,
            [&] { return vertexShaders.Add(device->CreateVertexShader(desc)); }
        );
    }

    PixelShaderHandle Session::GetOrCreatePixelShader(const ShaderDesc& desc)
    {
        return getOrCreate(
            pixelShaderHandles,
            desc,
            [&] { return pixelShaders.Add(device->CreatePixelShader(desc)); }
        );
    }

    ComputeShaderHandle Session::GetOrCreateComputeShader(const ShaderDesc& desc)
    {
        return getOrCreate(
            computeShaderHandles,
            desc,
            [&] { return computeShaders.Add(device->CreateComputeShader(desc)); }
        );
    }
}
