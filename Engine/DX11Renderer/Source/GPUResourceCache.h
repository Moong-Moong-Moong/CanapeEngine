#pragma once

#include "MeshAssetId.h"
#include "MeshHandle.h"
#include "MeshData.h"
#include "TextureData.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Canape
{
    // Renderer-internal, single-threaded cache. Owns resources, never Device/Context.
    // Returned resource pointers are borrowed until removal or renderer shutdown.
    class GPUResourceCache
    {
    public:
        struct GpuMesh
        {
            Microsoft::WRL::ComPtr<ID3D11Buffer> VertexBuffer;
            Microsoft::WRL::ComPtr<ID3D11Buffer> IndexBuffer;
            uint32_t IndexCount = 0;
            uint32_t Stride = 0;
            VertexFormat Format = VertexFormat::Position;
            std::vector<SubMesh> SubMeshes;
        };

        struct GpuTexture
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShaderResourceView;
        };

        GPUResourceCache() = default;
        GPUResourceCache(const GPUResourceCache&) = delete;
        GPUResourceCache& operator=(const GPUResourceCache&) = delete;

        MeshHandle CreateMesh(ID3D11Device* device, const void* vertices, size_t vertexCount,
            uint32_t stride, VertexFormat format, std::span<const uint32_t> indices,
            std::span<const SubMesh> submeshes, std::string& error);
        MeshHandle RegisterMesh(ID3D11Device* device, MeshAssetId assetId, const void* vertices,
            size_t vertexCount, uint32_t stride, VertexFormat format, std::span<const uint32_t> indices,
            std::span<const SubMesh> submeshes, std::string& error);
        MeshHandle FindMesh(MeshAssetId assetId) const;
        const GpuMesh* GetMesh(MeshHandle mesh) const;
        bool IsMeshValid(MeshHandle mesh) const;
        void DestroyMesh(MeshHandle mesh);

        TextureHandle RegisterTexture(ID3D11Device* device, ID3D11DeviceContext* context,
            TextureAssetId assetId, uint32_t width, uint32_t height, TextureFormat format,
            std::span<const uint8_t> pixels, std::string& error);
        TextureHandle FindTexture(TextureAssetId assetId) const;
        const GpuTexture* GetTexture(TextureHandle texture) const;
        bool IsTextureValid(TextureHandle texture) const;
        void DestroyTexture(TextureHandle texture);

        bool CreateWhiteTexture(ID3D11Device* device, ID3D11DeviceContext* context, std::string& error);
        ID3D11ShaderResourceView* GetWhiteTextureView() const;

    private:
        bool CreateGpuTexture(ID3D11Device* device, ID3D11DeviceContext* context,
            uint32_t width, uint32_t height, const uint8_t* pixels, GpuTexture& texture, std::string& error);

        std::unordered_map<uint64_t, GpuMesh> Meshes;
        std::unordered_map<uint64_t, MeshHandle> RegisteredMeshes;
        std::unordered_map<uint64_t, GpuTexture> Textures;
        std::unordered_map<uint64_t, TextureHandle> RegisteredTextures;
        GpuTexture WhiteTexture;
    };
}
