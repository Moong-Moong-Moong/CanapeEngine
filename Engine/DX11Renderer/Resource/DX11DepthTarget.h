#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace Canape
{
    // Owns a single-sample 2D depth target with an optional shader view. Views are borrowed.
    class DX11DepthTarget
    {
    public:
        DX11DepthTarget() = default;
        DX11DepthTarget(const DX11DepthTarget&) = delete;
        DX11DepthTarget& operator=(const DX11DepthTarget&) = delete;

        // A failed recreation leaves the previous target intact.
        bool Create(ID3D11Device* device, uint32_t width, uint32_t height,
            bool shaderReadable, std::string& error);
        void Reset();

        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        bool IsShaderReadable() const { return m_ShaderReadable; }
        ID3D11Texture2D* GetTexture() const { return m_Texture.Get(); }
        ID3D11DepthStencilView* GetDepthStencilView() const { return m_DepthStencilView.Get(); }
        ID3D11ShaderResourceView* GetShaderResourceView() const { return m_ShaderResourceView.Get(); }

    private:
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthStencilView;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ShaderResourceView;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool m_ShaderReadable = false;
    };
}
