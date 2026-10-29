#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

#include "DX11ResourceId.h"

namespace Canape
{
    struct DX11TextureResource
    {
        TextureResourceId Id;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShaderResourceView;

        uint32_t Width = 0;
        uint32_t Height = 0;
    };
}
