#include "DX11Simulation/Buffer.h"

#include <cassert>
#include <cstring>

namespace Canape
{
    Buffer::Buffer(
        ID3D11Device& device,
        ID3D11DeviceContext& context,
        const D3D11_BUFFER_DESC& desc,
        const D3D11_SUBRESOURCE_DATA* initialData,
        std::string_view name
    )
        : context(context)
    {
        ThrowIfFailed(
            device.CreateBuffer(
                &desc,
                initialData,
                &buffer
            ),
            "ID3D11Device::CreateBuffer"
        );

    #if CANAPE_RENDER_DEBUG
        if(!name.empty())
        {
            buffer->SetPrivateData(
                WKPDID_D3DDebugObjectName,
                static_cast<UINT>(name.length()),
                name.data()
            );
        }
    #endif

        const bool structured = (desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0;
        const auto elementCount = structured ?
            desc.ByteWidth / desc.StructureByteStride :
            0;
        if(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
        {
            assert(structured);

            D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc {
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D11_SRV_DIMENSION_BUFFER
            };
            viewDesc.Buffer.NumElements = elementCount;
            ThrowIfFailed(
                device.CreateShaderResourceView(
                    buffer.Get(),
                    &viewDesc,
                    &shaderResourceView
                ),
                "ID3D11Device::CreateShaderResourceView(buffer)"
            );
        }
        if(desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
        {
            assert(structured);

            D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc {
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D11_UAV_DIMENSION_BUFFER
            };
            viewDesc.Buffer.NumElements = elementCount;
            ThrowIfFailed(
                device.CreateUnorderedAccessView(
                    buffer.Get(),
                    &viewDesc,
                    &unorderedAccessView
                ),
                "ID3D11Device::CreateUnorderedAccessView(buffer)"
            );
        }
    }

    D3D11_BUFFER_DESC Buffer::GetDesc() const
    {
        D3D11_BUFFER_DESC desc{};
        buffer->GetDesc(&desc);

        return desc;
    }

    void Buffer::Upload(std::span<const std::byte> data)
    {
        assert(GetDesc().Usage == D3D11_USAGE_DYNAMIC);
        assert(data.size() == GetDesc().ByteWidth);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        ThrowIfFailed(
            context.Map(
                buffer.Get(),
                0,
                D3D11_MAP_WRITE_DISCARD,
                0,
                &mapped
            ),
            "ID3D11DeviceContext::Map(buffer write)"
        );
        std::memcpy(
            mapped.pData,
            data.data(),
            data.size()
        );
        context.Unmap(
            buffer.Get(),
            0
        );
    }

    void Buffer::Download(std::span<std::byte> data) const
    {
        assert(data.size() == GetDesc().ByteWidth);

        const D3D11_BUFFER_DESC stagingDesc {
            .ByteWidth = static_cast<UINT>(data.size()),
            .Usage = D3D11_USAGE_STAGING,
            .CPUAccessFlags = D3D11_CPU_ACCESS_READ
        };
        DeviceRef d3dDevice;
        BufferRef staging;
        buffer->GetDevice(&d3dDevice);
        ThrowIfFailed(
            d3dDevice->CreateBuffer(
                &stagingDesc,
                nullptr,
                &staging
            ),
            "ID3D11Device::CreateBuffer(buffer download)"
        );

        context.CopyResource(
            staging.Get(),
            buffer.Get()
        );

        D3D11_MAPPED_SUBRESOURCE mapped{};
        ThrowIfFailed(
            context.Map(
                staging.Get(),
                0,
                D3D11_MAP_READ,
                0,
                &mapped
            ),
            "ID3D11DeviceContext::Map(buffer)"
        );
        std::memcpy(
            data.data(),
            mapped.pData,
            data.size()
        );
        context.Unmap(
            staging.Get(),
            0
        );
    }
}
