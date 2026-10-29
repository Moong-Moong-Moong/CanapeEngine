#include "DX11RenderState.h"

#include "DX11Device.h"

namespace Canape
{
    bool DX11RenderState::Initialize(Device& device)
    {
        ID3D11Device* d3dDevice = device.GetDevice();
        if (!d3dDevice)
        {
            return false;
        }

        if (!CreateDepthStencilStates(d3dDevice))
        {
            return false;
        }

        if (!CreateRasterizerStates(d3dDevice))
        {
            return false;
        }

        if (!CreateBlendStates(d3dDevice))
        {
            return false;
        }

        if (!CreateSampler(d3dDevice))
        {
            return false;
        }

        return true;
    }

    bool DX11RenderState::CreateDepthStencilStates(ID3D11Device* device)
    {
        D3D11_DEPTH_STENCIL_DESC ds = {};
        ds.StencilEnable = FALSE;
        ds.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
        ds.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;

        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D11_COMPARISON_LESS;
        if (FAILED(device->CreateDepthStencilState(&ds, m_depthStencilStates[ToIndex(DX11DepthState::DepthOn)].ReleaseAndGetAddressOf())))
        {
            return false;
        }

        ds.DepthEnable = FALSE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
        if (FAILED(device->CreateDepthStencilState(&ds, m_depthStencilStates[ToIndex(DX11DepthState::DepthOff)].ReleaseAndGetAddressOf())))
        {
            return false;
        }

        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = D3D11_COMPARISON_LESS;
        if (FAILED(device->CreateDepthStencilState(&ds, m_depthStencilStates[ToIndex(DX11DepthState::DepthWriteOff)].ReleaseAndGetAddressOf())))
        {
            return false;
        }

        return true;
    }

    bool DX11RenderState::CreateRasterizerStates(ID3D11Device* device)
    {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FrontCounterClockwise = FALSE;
        rd.DepthClipEnable = TRUE;

        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        if (FAILED(device->CreateRasterizerState(&rd, m_rasterizerStates[ToIndex(DX11RasterizerState::Solid)].ReleaseAndGetAddressOf())))
        {
            return false;
        }

        rd.FillMode = D3D11_FILL_WIREFRAME;
        rd.CullMode = D3D11_CULL_NONE;
        if (FAILED(device->CreateRasterizerState(&rd, m_rasterizerStates[ToIndex(DX11RasterizerState::Wireframe)].ReleaseAndGetAddressOf())))
        {
            return false;
        }

        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_BACK;
        if (FAILED(device->CreateRasterizerState(&rd, m_rasterizerStates[ToIndex(DX11RasterizerState::CullBack)].ReleaseAndGetAddressOf())))
        {
            return false;
        }

        rd.FillMode = D3D11_FILL_WIREFRAME;
        rd.CullMode = D3D11_CULL_BACK;
        if (FAILED(device->CreateRasterizerState(&rd, m_rasterizerStates[ToIndex(DX11RasterizerState::WireframeCullBack)].ReleaseAndGetAddressOf())))
        {
            return false;
        }

        return true;
    }

    bool DX11RenderState::CreateBlendStates(ID3D11Device* device)
    {
        D3D11_BLEND_DESC bd = {};
        bd.AlphaToCoverageEnable = FALSE;
        bd.IndependentBlendEnable = FALSE;
        bd.RenderTarget[0].BlendEnable = FALSE;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&bd, m_blendStates[ToIndex(DX11BlendState::Default)].ReleaseAndGetAddressOf())))
        {
            return false;
        }

        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (FAILED(device->CreateBlendState(&bd, m_blendStates[ToIndex(DX11BlendState::AlphaBlend)].ReleaseAndGetAddressOf())))
        {
            return false;
        }

        return true;
    }

    bool DX11RenderState::CreateSampler(ID3D11Device* device)
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_ANISOTROPIC;
        sd.MaxAnisotropy = 16;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MinLOD = 0.0f;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sd, m_sampler.ReleaseAndGetAddressOf())))
        {
            return false;
        }

        return true;
    }
}
