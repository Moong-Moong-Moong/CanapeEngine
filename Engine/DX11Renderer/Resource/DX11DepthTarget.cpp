#include "DX11Renderer/Resource/DX11DepthTarget.h"

#include <sstream>
#include <utility>

namespace Canape
{
    namespace
    {
        bool Fail(std::string& error, const char* operation, HRESULT result)
        {
            std::ostringstream stream;
            stream << operation << " failed (HRESULT 0x" << std::hex
                   << static_cast<unsigned long>(result) << ')';
            error = stream.str();
            return false;
        }
    }

    bool DX11DepthTarget::Create(ID3D11Device* device, uint32_t width, uint32_t height,
        bool shaderReadable, std::string& error)
    {
        if (!device || width == 0 || height == 0 ||
            width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        {
            error = "Depth target requires a device and valid Texture2D dimensions";
            return false;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = shaderReadable ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL |
            (shaderReadable ? D3D11_BIND_SHADER_RESOURCE : 0);

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        HRESULT result = device->CreateTexture2D(&desc, nullptr, &texture);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateTexture2D(depth target)", result);

        D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc{};
        depthViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        depthViewDesc.Texture2D.MipSlice = 0;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilView;
        result = device->CreateDepthStencilView(texture.Get(), &depthViewDesc, &depthStencilView);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateDepthStencilView(depth target)", result);

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView;
        if (shaderReadable)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC shaderViewDesc{};
            shaderViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
            shaderViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            shaderViewDesc.Texture2D.MostDetailedMip = 0;
            shaderViewDesc.Texture2D.MipLevels = 1;
            result = device->CreateShaderResourceView(texture.Get(), &shaderViewDesc, &shaderResourceView);
            if (FAILED(result)) return Fail(error, "ID3D11Device::CreateShaderResourceView(depth target)", result);
        }

        m_Texture = std::move(texture);
        m_DepthStencilView = std::move(depthStencilView);
        m_ShaderResourceView = std::move(shaderResourceView);
        m_Width = width;
        m_Height = height;
        m_ShaderReadable = shaderReadable;
        error.clear();
        return true;
    }

    void DX11DepthTarget::Reset()
    {
        m_ShaderResourceView.Reset();
        m_DepthStencilView.Reset();
        m_Texture.Reset();
        m_Width = 0;
        m_Height = 0;
        m_ShaderReadable = false;
    }
}
