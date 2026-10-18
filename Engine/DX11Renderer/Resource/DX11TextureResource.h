#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace Canape
{
    // GPU texture and its shader view for one registered texture. Owned by DX11ResourceCache.
    struct DX11TextureResource
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShaderResourceView;
    };
}
