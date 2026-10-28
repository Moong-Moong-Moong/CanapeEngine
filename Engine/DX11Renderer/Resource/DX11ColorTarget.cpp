#include "DX11Renderer/Resource/DX11ColorTarget.h"

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

    bool DX11ColorTarget::Create(ID3D11Device* device, uint32_t width, uint32_t height,
        DXGI_FORMAT format, std::string& error)
    {
        if (!device || width == 0 || height == 0 ||
            width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        {
            error = "Color target requires a device and valid Texture2D dimensions";
            return false;
        }

        UINT support = 0;
        const HRESULT supportResult = device->CheckFormatSupport(format, &support);
        if (FAILED(supportResult)) return Fail(error, "ID3D11Device::CheckFormatSupport(color target)", supportResult);
        constexpr UINT requiredSupport = D3D11_FORMAT_SUPPORT_TEXTURE2D |
            D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
        if ((support & requiredSupport) != requiredSupport)
        {
            error = "Color target format must support Texture2D rendering and shader sampling";
            return false;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        HRESULT result = device->CreateTexture2D(&desc, nullptr, &texture);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateTexture2D(color target)", result);

        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView;
        result = device->CreateRenderTargetView(texture.Get(), nullptr, &renderTargetView);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateRenderTargetView(color target)", result);

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView;
        result = device->CreateShaderResourceView(texture.Get(), nullptr, &shaderResourceView);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateShaderResourceView(color target)", result);

        m_Texture = std::move(texture);
        m_RenderTargetView = std::move(renderTargetView);
        m_ShaderResourceView = std::move(shaderResourceView);
        m_Width = width;
        m_Height = height;
        m_Format = format;
        error.clear();
        return true;
    }

    void DX11ColorTarget::Reset()
    {
        m_ShaderResourceView.Reset();
        m_RenderTargetView.Reset();
        m_Texture.Reset();
        m_Width = 0;
        m_Height = 0;
        m_Format = DXGI_FORMAT_UNKNOWN;
    }
}
