#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace Canape
{
    // Owns a single-sample, shader-readable 2D color target. Views are borrowed.
    class DX11ColorTarget
    {
    public:
        DX11ColorTarget() = default;
        DX11ColorTarget(const DX11ColorTarget&) = delete;
        DX11ColorTarget& operator=(const DX11ColorTarget&) = delete;

        // A failed recreation leaves the previous target intact.
        bool Create(ID3D11Device* device, uint32_t width, uint32_t height,
            DXGI_FORMAT format, std::string& error);
        void Reset();

        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        DXGI_FORMAT GetFormat() const { return m_Format; }
        ID3D11Texture2D* GetTexture() const { return m_Texture.Get(); }
        ID3D11RenderTargetView* GetRenderTargetView() const { return m_RenderTargetView.Get(); }
        ID3D11ShaderResourceView* GetShaderResourceView() const { return m_ShaderResourceView.Get(); }

    private:
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ShaderResourceView;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;
    };
}
