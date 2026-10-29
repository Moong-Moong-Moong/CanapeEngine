#pragma once

#include <wrl/client.h>
#include <dxgi1_2.h>
#include <D3D11.h>
#include <span>

//***** Render에 필요한 GPU 자원 할당
namespace Canape
{
    using Microsoft::WRL::ComPtr;

    class Device
    {
    public:
        bool Initialize();

        ID3D11Device* GetDevice() const { return m_device.Get(); }
        ID3D11DeviceContext* GetDeviceContext() const { return m_deviceContext.Get(); }
        IDXGIFactory2* GetFactory() const { return m_factory.Get(); }
        D3D_FEATURE_LEVEL GetFeatureLevel() const { return m_FeatureLevel; }
        const DXGI_ADAPTER_DESC1& GetAdapterDesc() const { return m_AdapterDesc; }

        bool CreateVertexBuffer(const void* pData, UINT size, ID3D11Buffer** ppVB);
        bool CreateIndexBuffer(const void* pData, UINT size, ID3D11Buffer** ppIB);
        ComPtr<ID3D11Texture2D> CreateTexture2D(const D3D11_TEXTURE2D_DESC& desc, const D3D11_SUBRESOURCE_DATA* initData);
        bool CreateShaderResourceView(ID3D11Resource* resource, ID3D11ShaderResourceView** ppSRV);
        ComPtr<ID3D11InputLayout> CreateInputLayout(std::span<const D3D11_INPUT_ELEMENT_DESC> elements, std::span<const uint8_t> vsBytecode);
        bool CreateConstantBuffer(UINT size, bool dynamic, const void* pData, ID3D11Buffer** ppCB);
        bool UpdateConstantBuffer(ID3D11Buffer* pCB, const void* pData, UINT size);

    private:
        bool CreateFactory();
        static UINT AlignCBSize(UINT size);

    private:
        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_deviceContext;
        ComPtr<IDXGIFactory2> m_factory; // Direct3D 아래에서 GPU 하드웨어와 화면 출력을 관리하는 Windows 계층
        D3D_FEATURE_LEVEL m_FeatureLevel = D3D_FEATURE_LEVEL_11_0; 	// GPU가 지원하는 Direct3D 기능 수준을 나타내는 열거형
        DXGI_ADAPTER_DESC1 m_AdapterDesc = {}; 	// GPU 정보를 담는 구조체
    };
}