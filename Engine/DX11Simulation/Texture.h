#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "DX11Simulation/D3D.h"

namespace Canape
{
    class Texture
    {
    private:
        Texture2DRef texture;

        ID3D11DeviceContext& context;

        ReadViewRef shaderResourceView;
        WriteViewRef unorderedAccessView;

    public:
        // Starts zeroed when it has the UAV and no initial data.
        Texture(
            ID3D11Device&,
            ID3D11DeviceContext&,
            const D3D11_TEXTURE2D_DESC&,
            const D3D11_SUBRESOURCE_DATA* initialData = nullptr,
            std::string_view name = {}
        );

        // Copies the first mip out through a staging copy, row after row without padding; data holds Height rows.
        void Download(std::span<std::byte> data) const;

        D3D11_TEXTURE2D_DESC GetDesc() const;

        ID3D11Texture2D* Get() const
        {
            return texture.Get();
        }

        // Each is null unless the texture has the matching bind flag.
        ID3D11ShaderResourceView* GetSRV() const
        {
            return shaderResourceView.Get();
        }
        ID3D11UnorderedAccessView* GetUAV() const
        {
            return unorderedAccessView.Get();
        }
    };
}
