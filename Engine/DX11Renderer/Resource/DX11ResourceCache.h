#pragma once

#include "Asset/AssetId.h"
#include "Asset/Material/MaterialDesc.h"
#include "Asset/Mesh/MeshData.h"
#include "Asset/Texture/TextureData.h"
#include "Renderer/Handle/MaterialHandle.h"
#include "Renderer/Handle/MeshHandle.h"
#include "Renderer/Handle/TextureHandle.h"
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

        MeshHandle CreateMesh(ID3D11Device* device, const void* vertices, size_t vertexCount,
            uint32_t stride, VertexFormat format, std::span<const uint32_t> indices,
            std::span<const SubMesh> submeshes, std::string& error);
        MeshHandle RegisterMesh(ID3D11Device* device, MeshAssetId assetId, const void* vertices,
            size_t vertexCount, uint32_t stride, VertexFormat format, std::span<const uint32_t> indices,
            std::span<const SubMesh> submeshes, std::string& error);
        MeshHandle FindMesh(MeshAssetId assetId) const;
        const DX11MeshResource* GetMesh(MeshHandle mesh) const;
        bool IsMeshValid(MeshHandle mesh) const;
        void DestroyMesh(MeshHandle mesh);

        TextureHandle RegisterTexture(ID3D11Device* device, ID3D11DeviceContext* context,
            TextureAssetId assetId, uint32_t width, uint32_t height, TextureFormat format,
            std::span<const uint8_t> pixels, std::string& error);
        TextureHandle FindTexture(TextureAssetId assetId) const;
        const DX11TextureResource* GetTexture(TextureHandle texture) const;
        bool IsTextureValid(TextureHandle texture) const;
        // Also invalidates materials referencing the texture; CPU assets can re-register.
        void DestroyTexture(TextureHandle texture);

        // Registration copies the descriptor once. First registration wins until removal/reset.
        MaterialHandle RegisterMaterial(MaterialAssetId assetId, const MaterialDesc& desc,
            TextureHandle baseColorTexture, std::string& error);
        MaterialHandle FindMaterial(MaterialAssetId assetId) const;
        const DX11MaterialResource* GetMaterial(MaterialHandle material) const;
        bool IsMaterialValid(MaterialHandle material) const;
        void DestroyMaterial(MaterialHandle material);

        bool CreateWhiteTexture(ID3D11Device* device, ID3D11DeviceContext* context, std::string& error);
        ID3D11ShaderResourceView* GetWhiteTextureView() const;

    private:
        bool CreateTextureResource(ID3D11Device* device, ID3D11DeviceContext* context,
            uint32_t width, uint32_t height, const uint8_t* pixels, DX11TextureResource& texture, std::string& error);

        std::unordered_map<uint64_t, DX11MeshResource> Meshes;
        std::unordered_map<uint64_t, MeshHandle> RegisteredMeshes;
        std::unordered_map<uint64_t, DX11TextureResource> Textures;
        std::unordered_map<uint64_t, TextureHandle> RegisteredTextures;
        std::unordered_map<uint64_t, DX11MaterialResource> Materials;
        std::unordered_map<uint64_t, MaterialHandle> RegisteredMaterials;
        DX11TextureResource WhiteTexture;
    };
}
