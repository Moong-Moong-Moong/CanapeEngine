#pragma once

#include "DX11Renderer/Data/MaterialDesc.h"
#include "DX11Renderer/Data/MeshData.h"
#include "DX11Renderer/Data/TextureData.h"
#include "DX11Renderer/Resource/DX11ResourceId.h"
#include "DX11Renderer/Resource/DX11MaterialResource.h"
#include "DX11Renderer/Resource/DX11MeshResource.h"
#include "DX11Renderer/Resource/DX11TextureResource.h"

#include <d3d11.h>
#include <span>
#include <string>
#include <unordered_map>

namespace Canape
{
    class DX11ResourceCache
    {
    public:
        DX11ResourceCache() = default;
        DX11ResourceCache(const DX11ResourceCache&) = delete;
        DX11ResourceCache& operator=(const DX11ResourceCache&) = delete;

        MeshResourceId CreateMesh(ID3D11Device* device, const void* vertices, size_t vertexCount,
            uint32_t stride, VertexFormat format, std::span<const uint32_t> indices,
            std::span<const SubMesh> submeshes, std::string& error);
        const DX11MeshResource* GetMesh(MeshResourceId mesh) const;
        bool IsMeshValid(MeshResourceId mesh) const;
        void DestroyMesh(MeshResourceId mesh);

        TextureResourceId CreateTexture(ID3D11Device* device, ID3D11DeviceContext* context,
            uint32_t width, uint32_t height, TextureFormat format,
            std::span<const uint8_t> pixels, std::string& error);
        const DX11TextureResource* GetTexture(TextureResourceId texture) const;
        bool IsTextureValid(TextureResourceId texture) const;
        // Also invalidates materials referencing the texture; CPU assets can re-create them.
        void DestroyTexture(TextureResourceId texture);

        // Creation copies the descriptor once. Destroy/re-create to apply CPU edits.
        MaterialResourceId CreateMaterial(const MaterialDesc& desc, TextureResourceId baseColorTexture, std::string& error);
        const DX11MaterialResource* GetMaterial(MaterialResourceId material) const;
        bool IsMaterialValid(MaterialResourceId material) const;
        void DestroyMaterial(MaterialResourceId material);

        bool CreateWhiteTexture(ID3D11Device* device, ID3D11DeviceContext* context, std::string& error);
        ID3D11ShaderResourceView* GetWhiteTextureView() const;

    private:
        bool CreateTextureResource(ID3D11Device* device, ID3D11DeviceContext* context,
            uint32_t width, uint32_t height, const uint8_t* pixels, DX11TextureResource& texture, std::string& error);

        std::unordered_map<uint64_t, DX11MeshResource> Meshes;
        std::unordered_map<uint64_t, DX11TextureResource> Textures;
        std::unordered_map<uint64_t, DX11MaterialResource> Materials;
        DX11TextureResource WhiteTexture;
    };
}
