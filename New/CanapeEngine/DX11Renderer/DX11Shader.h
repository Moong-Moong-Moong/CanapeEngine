#pragma once

#include <wrl/client.h>
#include <D3D11.h>
#include <span>
#include <string>

namespace Canape
{
    class Device;

    class DX11Shader
    {
    public:
        DX11Shader() = default;
        DX11Shader(const DX11Shader&) = delete;
        DX11Shader& operator=(const DX11Shader&) = delete;

        bool Load(Device& device, const wchar_t* path, const char* vsEntry, const char* psEntry,
            std::span<const D3D11_INPUT_ELEMENT_DESC> layout, UINT constantBufferSize, std::string& error);

        void Bind(ID3D11DeviceContext* context) const;
        bool UpdateConstants(Device& device, const void* data, UINT size) const;

    private:
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    };
}
