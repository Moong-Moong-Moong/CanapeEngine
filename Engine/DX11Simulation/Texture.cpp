#include "DX11Simulation/Texture.h"

#include <cassert>
#include <cstring>

namespace Canape
{
    Texture::Texture(
        ID3D11Device& device,
        ID3D11DeviceContext& context,
        const D3D11_TEXTURE2D_DESC& desc,
        const D3D11_SUBRESOURCE_DATA* initialData,
        std::string_view name
    )
        : context(context)
    {
        ThrowIfFailed(
            device.CreateTexture2D(
                &desc,
                initialData,
                &texture
            ),
            "ID3D11Device::CreateTexture2D"
        );

    #if CANAPE_RENDER_DEBUG
        if(!name.empty())
        {
            texture->SetPrivateData(
                WKPDID_D3DDebugObjectName,
                static_cast<UINT>(name.length()),
                name.data()
            );
        }
    #endif

        if(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
        {
            ThrowIfFailed(
                device.CreateShaderResourceView(
                    texture.Get(),
                    nullptr,
                    &shaderResourceView
                ),
                "ID3D11Device::CreateShaderResourceView(texture)"
            );
        }
        if(desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
        {
            ThrowIfFailed(
                device.CreateUnorderedAccessView(
                    texture.Get(),
                    nullptr,
                    &unorderedAccessView
                ),
                "ID3D11Device::CreateUnorderedAccessView(texture)"
            );
        }
    }

    D3D11_TEXTURE2D_DESC Texture::GetDesc() const
    {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);

        return desc;
    }

    void Texture::Download(std::span<std::byte> data) const
    {
        const auto desc = GetDesc();
        const auto height = desc.Height;
        assert(data.size() % height == 0);

        const D3D11_TEXTURE2D_DESC stagingDesc {
            .Width = desc.Width,
            .Height = height,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = desc.Format,
            .SampleDesc = { .Count = 1 },
            .Usage = D3D11_USAGE_STAGING,
            .CPUAccessFlags = D3D11_CPU_ACCESS_READ
        };
        DeviceRef d3dDevice;
        Texture2DRef staging;
        texture->GetDevice(&d3dDevice);
        ThrowIfFailed(
            d3dDevice->CreateTexture2D(
                &stagingDesc,
                nullptr,
                &staging
            ),
            "ID3D11Device::CreateTexture2D(texture download)"
        );

        context.CopySubresourceRegion(
            staging.Get(),
            0,
            0,
            0,
            0,
            texture.Get(),
            0,
            nullptr
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
            "ID3D11DeviceContext::Map(texture)"
        );
        const auto rowBytes = data.size() / desc.Height;
        assert(rowBytes <= mapped.RowPitch);
        for(auto row = 0u; row < desc.Height; ++row)
        {
            std::memcpy(
                data.data() + row * rowBytes,
                static_cast<const std::byte*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch,
                rowBytes
            );
        }
        context.Unmap(staging.Get(), 0);
    }
}
