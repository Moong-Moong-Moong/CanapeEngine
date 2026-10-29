#pragma once

#include <wrl/client.h>
#include <dxgi1_2.h>
#include <D3D11.h>
#include <cstdint>

namespace Canape
{
    class Device;

    class DX11SwapChain
    {
    public:
        explicit DX11SwapChain(Device& device);
        DX11SwapChain(const DX11SwapChain&) = delete;
        DX11SwapChain& operator=(const DX11SwapChain&) = delete;

        bool Initialize(HWND hwnd, uint32_t width, uint32_t height);

        void SetViewport();
        void ClearBackBuffer(const float color[4], UINT flags, float depth, UINT8 stencil);
        HRESULT Flip(bool vsync);

        ID3D11RenderTargetView* GetRenderTargetView() const { return m_renderTargetView.Get(); }
        ID3D11DepthStencilView* GetDepthStencilView() const { return m_depthStencilView.Get(); }
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }

    private:
        bool CreateSwapChain(HWND hwnd);
        bool CreateRenderTarget();
        bool CreateDepthStencil();

    private:
        Device& m_Device;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthStencil;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencilView;

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
    };
}