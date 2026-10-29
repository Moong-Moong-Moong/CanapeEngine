#include "DX11Shader.h"

#include "DX11Device.h"

#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace Canape
{
    namespace
    {
        ComPtr<ID3DBlob> CompileShader(const wchar_t* path, const char* entry, const char* target, std::string& error)
        {
            UINT flags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
#ifdef _DEBUG
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

            ComPtr<ID3DBlob> code;
            ComPtr<ID3DBlob> errorBlob;
            HRESULT hr = D3DCompileFromFile(path, nullptr, nullptr, entry, target, flags, 0,
                code.GetAddressOf(), errorBlob.GetAddressOf());
            if (FAILED(hr))
            {
                error = errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "shader file not found";
                code.Reset();
            }

            return code;
        }
    }

    bool DX11Shader::Load(Device& device, const wchar_t* path, const char* vsEntry, const char* psEntry,
        std::span<const D3D11_INPUT_ELEMENT_DESC> layout, UINT constantBufferSize, std::string& error)
    {
        ID3D11Device* d3dDevice = device.GetDevice();
        if (!d3dDevice)
        {
            error = "D3D11 device is not initialized";
            return false;
        }

        ComPtr<ID3DBlob> vsCode = CompileShader(path, vsEntry, "vs_5_0", error);
        if (!vsCode)
        {
            return false;
        }

        ComPtr<ID3DBlob> psCode = CompileShader(path, psEntry, "ps_5_0", error);
        if (!psCode)
        {
            return false;
        }

        if (FAILED(d3dDevice->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr,
            m_vertexShader.ReleaseAndGetAddressOf())))
        {
            error = "vertex shader creation failed";
            return false;
        }

        if (FAILED(d3dDevice->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr,
            m_pixelShader.ReleaseAndGetAddressOf())))
        {
            error = "pixel shader creation failed";
            return false;
        }

        m_inputLayout = device.CreateInputLayout(layout,
            std::span(static_cast<const uint8_t*>(vsCode->GetBufferPointer()), vsCode->GetBufferSize()));
        if (!m_inputLayout)
        {
            error = "input layout creation failed";
            return false;
        }

        if (!device.CreateConstantBuffer(constantBufferSize, true, nullptr, m_constantBuffer.ReleaseAndGetAddressOf()))
        {
            error = "constant buffer creation failed";
            return false;
        }

        return true;
    }

    void DX11Shader::Bind(ID3D11DeviceContext* context) const
    {
        ID3D11Buffer* constantBuffer = m_constantBuffer.Get();

        context->IASetInputLayout(m_inputLayout.Get());
        context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &constantBuffer);
        context->PSSetConstantBuffers(0, 1, &constantBuffer);
    }

    bool DX11Shader::UpdateConstants(Device& device, const void* data, UINT size) const
    {
        return device.UpdateConstantBuffer(m_constantBuffer.Get(), data, size);
    }
}
