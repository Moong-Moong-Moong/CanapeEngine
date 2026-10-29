#pragma once

#include "DX11Renderer/Data/MeshData.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

namespace Canape
{
    // GPU buffers for one registered mesh. Owned by DX11ResourceCache.
    struct DX11MeshResource
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> VertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> IndexBuffer;
        uint32_t IndexCount = 0;
        uint32_t Stride = 0;
        VertexFormat Format = VertexFormat::Position;
        std::vector<SubMesh> SubMeshes;
    };
}
