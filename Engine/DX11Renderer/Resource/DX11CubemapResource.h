#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace Canape
{
    struct DX11CubemapResource
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShaderResourceView;
        uint32_t MipLevels = 0;
    };
}
