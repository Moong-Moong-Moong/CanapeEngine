#include "DX11Device.h"

#include <cstring>
#include <iterator>

#pragma comment(lib, "d3d11.lib")

namespace Canape
{
    bool Device::Initialize()
    {
        if (m_device)
        {
            return true;
        }

        const D3D_FEATURE_LEVEL featureLevels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
            m_device.ReleaseAndGetAddressOf(), &m_FeatureLevel, m_deviceContext.ReleaseAndGetAddressOf());

        if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG))
        {
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
                m_device.ReleaseAndGetAddressOf(), &m_FeatureLevel, m_deviceContext.ReleaseAndGetAddressOf());
        }

        if (FAILED(hr))
        {
            m_device.Reset();
            m_deviceContext.Reset();
            return false;
        }

        if (!CreateFactory())
        {
            m_device.Reset();
            m_deviceContext.Reset();
            return false;
        }

        return true;
    }

    bool Device::CreateFactory()
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(m_device.As(&dxgiDevice)))
        {
            return false;
        }

        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())))
        {
            return false;
        }

        if (FAILED(adapter->GetParent(IID_PPV_ARGS(m_factory.ReleaseAndGetAddressOf()))))
        {
            return false;
        }

        return true;
    }

    ComPtr<ID3D11Texture2D> Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC& desc, const D3D11_SUBRESOURCE_DATA* initData)
    {
        ComPtr<ID3D11Texture2D> texture;
        if (!m_device)
        {
            return texture;
        }

        if (FAILED(m_device->CreateTexture2D(&desc, initData, texture.GetAddressOf())))
        {
            texture.Reset();
        }

        return texture;
    }

    bool Device::CreateShaderResourceView(ID3D11Resource* resource, ID3D11ShaderResourceView** ppSRV)
    {
        if (!m_device || !resource || !ppSRV)
        {
            return false;
        }

        HRESULT hr = m_device->CreateShaderResourceView(resource, nullptr, ppSRV);
        if (FAILED(hr))
        {
            return false;
        }

        return true;
    }

    bool Device::CreateVertexBuffer(const void* pData, UINT size, ID3D11Buffer** ppVB)
    {
        if (!m_device || !pData || size == 0 || !ppVB)
        {
            return false;
        }

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = size;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA rd = {};
        rd.pSysMem = pData;

        HRESULT hr = m_device->CreateBuffer(&bd, &rd, ppVB);
        if (FAILED(hr))
        {
            return false;
        }

        return true;
    }

    bool Device::CreateIndexBuffer(const void* pData, UINT size, ID3D11Buffer** ppIB)
    {
        if (!m_device || !pData || size == 0 || !ppIB)
        {
            return false;
        }

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = size;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA rd = {};
        rd.pSysMem = pData;

        HRESULT hr = m_device->CreateBuffer(&bd, &rd, ppIB);
        if (FAILED(hr))
        {
            return false;
        }

        return true;
    }

    UINT Device::AlignCBSize(UINT size)
    {
        return (size + 15u) & ~15u;
    }

    bool Device::CreateConstantBuffer(UINT size, bool dynamic, const void* pData, ID3D11Buffer** ppCB)
    {
        if (!m_device || size == 0 || size > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16 || !ppCB)
        {
            return false;
        }

        const UINT sizeAligned = AlignCBSize(size);
        if (pData && sizeAligned != size)
        {
            return false;
        }

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeAligned;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;

        D3D11_SUBRESOURCE_DATA rd = {};
        rd.pSysMem = pData;

        HRESULT hr = m_device->CreateBuffer(&bd, pData ? &rd : nullptr, ppCB);
        if (FAILED(hr))
        {
            return false;
        }

        return true;
    }

    bool Device::UpdateConstantBuffer(ID3D11Buffer* pCB, const void* pData, UINT size)
    {
        if (!m_deviceContext || !pCB || !pData || size == 0)
        {
            return false;
        }

        D3D11_BUFFER_DESC bd = {};
        pCB->GetDesc(&bd);
        if (size > bd.ByteWidth)
        {
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_deviceContext->Map(pCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
        {
            return false;
        }

        std::memcpy(mapped.pData, pData, size);
        m_deviceContext->Unmap(pCB, 0);

        return true;
    }

    ComPtr<ID3D11InputLayout> Device::CreateInputLayout(std::span<const D3D11_INPUT_ELEMENT_DESC> elements, std::span<const uint8_t> vsBytecode)
    {
        ComPtr<ID3D11InputLayout> inputLayout;
        if (!m_device || elements.empty() || vsBytecode.empty())
        {
            return inputLayout;
        }

        HRESULT hr = m_device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()),
            vsBytecode.data(), vsBytecode.size(), inputLayout.GetAddressOf());
        if (FAILED(hr))
        {
            inputLayout.Reset();
        }

        return inputLayout;
    }
}
