#include "DX11Simulation/Device.h"

namespace Canape
{
    Device::Device(ID3D11Device& device, ID3D11DeviceContext& context)
        : device(&device), context(&context)
    {
    }

    Buffer Device::CreateBuffer(
        const D3D11_BUFFER_DESC& desc,
        const D3D11_SUBRESOURCE_DATA* initialData,
        std::string_view name)
    {
        return Buffer(*getDevice(), *getContext(), desc, initialData, name);
    }

    Texture Device::CreateTexture(
        const D3D11_TEXTURE2D_DESC& desc,
        const D3D11_SUBRESOURCE_DATA* initialData,
        std::string_view name)
    {
        return Texture(*getDevice(), *getContext(), desc, initialData, name);
    }

    SamplerStateRef Device::CreateSamplerState(
        const D3D11_SAMPLER_DESC& desc,
        std::string_view name)
    {
        SamplerStateRef sampler = nullptr;
        ThrowIfFailed(
            device->CreateSamplerState(
                &desc,
                &sampler
            ),
            "ID3D11Device::CreateSamplerState"
        );

    #if CANAPE_RENDER_DEBUG
        if(!name.empty())
        {
            sampler->SetPrivateData(
                WKPDID_D3DDebugObjectName,
                static_cast<UINT>(name.length()),
                name.data()
            );
        }
    #endif

        return sampler;
    }

    RasterizerStateRef Device::CreateRasterizerState(
        const D3D11_RASTERIZER_DESC& desc,
        std::string_view name)
    {
        RasterizerStateRef rasterizer = nullptr;
        ThrowIfFailed(
            device->CreateRasterizerState(
                &desc,
                &rasterizer
            ),
            "ID3D11Device::CreateRasterizerState"
        );

    #if CANAPE_RENDER_DEBUG
        if(!name.empty())
        {
            rasterizer->SetPrivateData(
                WKPDID_D3DDebugObjectName,
                static_cast<UINT>(name.length()),
                name.data()
            );
        }
    #endif

        return rasterizer;
    }

    DepthStencilStateRef Device::CreateDepthStencilState(
        const D3D11_DEPTH_STENCIL_DESC& desc,
        std::string_view name)
    {
        DepthStencilStateRef depthStencil = nullptr;
        ThrowIfFailed(
            device->CreateDepthStencilState(
                &desc,
                &depthStencil
            ),
            "ID3D11Device::CreateDepthStencilState"
        );

    #if CANAPE_RENDER_DEBUG
        if(!name.empty())
        {
            depthStencil->SetPrivateData(
                WKPDID_D3DDebugObjectName,
                static_cast<UINT>(name.length()),
                name.data()
            );
        }
    #endif

        return depthStencil;
    }

    BlendStateRef Device::CreateBlendState(
        const D3D11_BLEND_DESC& desc,
        std::string_view name)
    {
        BlendStateRef blendState = nullptr;
        ThrowIfFailed(
            device->CreateBlendState(
                &desc,
                &blendState
            ),
            "ID3D11Device::CreateBlendState"
        );

    #if CANAPE_RENDER_DEBUG
        if(!name.empty())
        {
            blendState->SetPrivateData(
                WKPDID_D3DDebugObjectName,
                static_cast<UINT>(name.length()),
                name.data()
            );
        }
    #endif

        return blendState;
    }

    VertexShaderRef Device::CreateVertexShader(
        const ShaderDesc& desc,
        std::string_view name)
    {
        return CompileVertexShader(*getDevice(), desc, name);
    }

    PixelShaderRef Device::CreatePixelShader(
        const ShaderDesc& desc,
        std::string_view name)
    {
        return CompilePixelShader(*getDevice(), desc, name);
    }

    ComputeShader Device::CreateComputeShader(
        const ShaderDesc& desc,
        std::string_view name)
    {
        return ComputeShader(*getDevice(), desc, name);
    }

    GpuTimer Device::CreateGpuTimer()
    {
        return GpuTimer(*getDevice(), *getContext());
    }
}
