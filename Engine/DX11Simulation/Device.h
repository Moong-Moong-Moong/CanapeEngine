#pragma once

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/D3D.h"
#include "DX11Simulation/Shader.h"
#include "DX11Simulation/Texture.h"

namespace Canape
{
    inline constexpr D3D11_SAMPLER_DESC LinearClampSamplerDesc()
    {
        return {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
            .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
            .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
            .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
            .MaxAnisotropy = 1,
            .ComparisonFunc = D3D11_COMPARISON_NEVER,
            .MaxLOD = D3D11_FLOAT32_MAX
        };
    }

    // Each Create throws std::runtime_error when the device refuses, or carrying the compiler output when
    // a shader does not compile.
    class Device
    {
    private:
        DeviceRef device;
        DeviceContextRef context;

    public:
        Device(ID3D11Device&, ID3D11DeviceContext&);

        Buffer CreateBuffer(
            const D3D11_BUFFER_DESC&,
            const D3D11_SUBRESOURCE_DATA* initialData = nullptr,
            std::string_view name = {}
        );
        Texture CreateTexture(
            const D3D11_TEXTURE2D_DESC&,
            const D3D11_SUBRESOURCE_DATA* initialData = nullptr,
            std::string_view name = {}
        );
        SamplerStateRef CreateSamplerState(
            const D3D11_SAMPLER_DESC&,
            std::string_view name = {}
        );

        RasterizerStateRef CreateRasterizerState(
            const D3D11_RASTERIZER_DESC&,
            std::string_view name = {}
        );
        DepthStencilStateRef CreateDepthStencilState(
            const D3D11_DEPTH_STENCIL_DESC&,
            std::string_view name = {}
        );
        BlendStateRef CreateBlendState(
            const D3D11_BLEND_DESC&,
            std::string_view name = {}
        );

        // path is relative to the executable unless absolute.
        VertexShaderRef CreateVertexShader(
            const ShaderDesc&,
            std::string_view name = {}
        );
        PixelShaderRef CreatePixelShader(
            const ShaderDesc&,
            std::string_view name = {}
        );
        ComputeShader CreateComputeShader(
            const ShaderDesc&,
            std::string_view name = {}
        );

    private:
        ID3D11Device* getDevice() const
        {
            return device.Get();
        };

        ID3D11DeviceContext* getContext() const
        {
            return context.Get();
        }
    };
}
