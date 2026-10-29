#include "DX11SwapChain.h"

#include "DX11Device.h"

namespace Canape
{
    DX11SwapChain::DX11SwapChain(Device& device)
        : m_Device(device)
    {
    }

    bool DX11SwapChain::Initialize(HWND hwnd, uint32_t width, uint32_t height)
    {
        if (!hwnd || width == 0 || height == 0 || !m_Device.GetDevice() || !m_Device.GetFactory())
        {
            return false;
        }

        m_Width = width;
        m_Height = height;

        if (!CreateSwapChain(hwnd))
        {
            return false;
        }

        if (!CreateRenderTarget())
        {
            return false;
        }

        if (!CreateDepthStencil())
        {
            return false;
        }

        return true;
    }

    bool DX11SwapChain::CreateSwapChain(HWND hwnd)
    {
        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = m_Width;
        sd.Height = m_Height;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        HRESULT hr = m_Device.GetFactory()->CreateSwapChainForHwnd(m_Device.GetDevice(), hwnd, &sd,
            nullptr, nullptr, m_swapChain.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            return false;
        }

        return true;
    }

    bool DX11SwapChain::CreateRenderTarget()
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_RENDER_TARGET_VIEW_DESC rd = {};
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

        hr = m_Device.GetDevice()->CreateRenderTargetView(backBuffer.Get(), &rd, m_renderTargetView.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            return false;
        }

        return true;
    }

    bool DX11SwapChain::CreateDepthStencil()
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = m_Width;
        td.Height = m_Height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_D32_FLOAT;
        td.SampleDesc.Count = 1;
        td.SampleDesc.Quality = 0;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        m_depthStencil = m_Device.CreateTexture2D(td, nullptr);
        if (!m_depthStencil)
        {
            return false;
        }

        D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
        dd.Format = td.Format;
        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dd.Texture2D.MipSlice = 0;

        HRESULT hr = m_Device.GetDevice()->CreateDepthStencilView(m_depthStencil.Get(), &dd, m_depthStencilView.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            return false;
        }

        return true;
    }

    void DX11SwapChain::SetViewport()
    {
        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(m_Width);
        vp.Height = static_cast<float>(m_Height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        m_Device.GetDeviceContext()->RSSetViewports(1, &vp);
    }

    void DX11SwapChain::ClearBackBuffer(const float color[4], UINT flags, float depth, UINT8 stencil)
    {
        ID3D11DeviceContext* context = m_Device.GetDeviceContext();
        context->ClearRenderTargetView(m_renderTargetView.Get(), color);
        context->ClearDepthStencilView(m_depthStencilView.Get(), flags, depth, stencil);
    }

    HRESULT DX11SwapChain::Flip(bool vsync)
    {
        return m_swapChain->Present(vsync ? 1 : 0, 0);
    }
}