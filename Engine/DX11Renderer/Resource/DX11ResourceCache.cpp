#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "DX11Renderer/Resource/DX11ResourceCache.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Canape
{
    namespace
    {
        bool Fail(std::string& error, const char* operation, HRESULT result)
        {
            std::ostringstream stream;
            stream << operation << " failed (HRESULT 0x" << std::hex
                   << static_cast<unsigned long>(result) << ')';
            error = stream.str();
            return false;
        }
    }

    MeshHandle DX11ResourceCache::CreateMesh(ID3D11Device* device, const void* vertices, size_t vertexCount,
        uint32_t stride, VertexFormat format, std::span<const uint32_t> indices,
        std::span<const SubMesh> submeshes, std::string& error)
    {
        if (!device)
        {
            error = "Initialize must be called before CreateMesh";
            return {};
        }
        if (const char* validationError = ValidateMeshIndices(vertexCount, indices, submeshes))
        {
            error = validationError;
            return {};
        }
        if (vertexCount > UINT32_MAX / stride || indices.size() > UINT32_MAX / sizeof(uint32_t))
        {
            error = "Mesh buffer byte size exceeds D3D11 limits";
            return {};
        }

        D3D11_BUFFER_DESC vertexBufferDesc{};
        vertexBufferDesc.ByteWidth = static_cast<UINT>(vertexCount * stride);
        vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertexData{ vertices };

        DX11MeshResource mesh;
        HRESULT result = device->CreateBuffer(&vertexBufferDesc, &vertexData, &mesh.VertexBuffer);
        if (FAILED(result))
        {
            Fail(error, "ID3D11Device::CreateBuffer(vertices)", result);
            return {};
        }

        D3D11_BUFFER_DESC indexBufferDesc{};
        indexBufferDesc.ByteWidth = static_cast<UINT>(indices.size_bytes());
        indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA indexData{ indices.data() };

        result = device->CreateBuffer(&indexBufferDesc, &indexData, &mesh.IndexBuffer);
        if (FAILED(result))
        {
            Fail(error, "ID3D11Device::CreateBuffer(indices)", result);
            return {};
        }

        mesh.IndexCount = static_cast<uint32_t>(indices.size());
        mesh.Stride = stride;
        mesh.Format = format;
        mesh.SubMeshes.assign(submeshes.begin(), submeshes.end());
        if (mesh.SubMeshes.empty()) mesh.SubMeshes.push_back({ 0, mesh.IndexCount, 0, 0 });

        // Process-wide, single-threaded allocation: never reuse handles after destroy/reinitialize.
        static uint64_t nextHandle = 1;
        if (nextHandle == 0)
        {
            error = "Mesh handle space exhausted";
            return {};
        }
        const MeshHandle handle{ nextHandle++ };
        Meshes.emplace(handle.Value, std::move(mesh));
        error.clear();
        return handle;
    }

    MeshHandle DX11ResourceCache::RegisterMesh(ID3D11Device* device, MeshAssetId assetId, const void* vertices,
        size_t vertexCount, uint32_t stride, VertexFormat format, std::span<const uint32_t> indices,
        std::span<const SubMesh> submeshes, std::string& error)
    {
        if (!assetId.IsValid())
        {
            error = "RegisterMesh requires a valid MeshAssetId";
            return {};
        }

        const MeshHandle existing = FindMesh(assetId);
        if (existing.IsValid())
        {
            error.clear();
            return existing;
        }

        // Discard a stale entry before retrying; failed uploads must not leave a registration.
        RegisteredMeshes.erase(assetId.value);
        const MeshHandle handle = CreateMesh(device, vertices, vertexCount, stride, format, indices, submeshes, error);
        if (handle.IsValid())
        {
            RegisteredMeshes.emplace(assetId.value, handle);
        }
        return handle;
    }

    MeshHandle DX11ResourceCache::FindMesh(MeshAssetId assetId) const
    {
        if (!assetId.IsValid()) return {};
        const auto found = RegisteredMeshes.find(assetId.value);
        return found != RegisteredMeshes.end() && IsMeshValid(found->second) ? found->second : MeshHandle{};
    }

    const DX11MeshResource* DX11ResourceCache::GetMesh(MeshHandle mesh) const
    {
        const auto found = Meshes.find(mesh.Value);
        return found != Meshes.end() ? &found->second : nullptr;
    }

    bool DX11ResourceCache::IsMeshValid(MeshHandle mesh) const
    {
        return mesh.IsValid() && GetMesh(mesh) != nullptr;
    }

    void DX11ResourceCache::DestroyMesh(MeshHandle mesh)
    {
        // Explicit removal is infrequent; scanning avoids a second registration index.
        std::erase_if(RegisteredMeshes, [mesh](const auto& entry) { return entry.second == mesh; });
        Meshes.erase(mesh.Value);
    }

    bool DX11ResourceCache::CreateTextureResource(ID3D11Device* device, ID3D11DeviceContext* context,
        uint32_t width, uint32_t height, const uint8_t* pixels, DX11TextureResource& texture, std::string& error)
    {
        constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        UINT support = 0;
        HRESULT result = device->CheckFormatSupport(format, &support);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CheckFormatSupport(sRGB texture)", result);
        if (!(support & D3D11_FORMAT_SUPPORT_TEXTURE2D) || !(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
        {
            error = "RGBA8 sRGB texture sampling is unsupported by this device";
            return false;
        }
        const bool generateMips = (width > 1 || height > 1) && (support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = generateMips ? 0 : 1;
        desc.ArraySize = 1;
        desc.Format = format; // Hardware sRGB -> linear conversion; alpha is not gamma converted.
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (generateMips ? D3D11_BIND_RENDER_TARGET : 0);
        desc.MiscFlags = generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        result = device->CreateTexture2D(&desc, nullptr, &texture.Texture);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateTexture2D(color)", result);
        result = device->CreateShaderResourceView(texture.Texture.Get(), nullptr, &texture.ShaderResourceView);
        if (FAILED(result)) return Fail(error, "ID3D11Device::CreateShaderResourceView(color)", result);
        context->UpdateSubresource(texture.Texture.Get(), 0, nullptr, pixels, width * 4, 0);
        if (generateMips) context->GenerateMips(texture.ShaderResourceView.Get());
        return true;
    }

    TextureHandle DX11ResourceCache::RegisterTexture(ID3D11Device* device, ID3D11DeviceContext* context,
        TextureAssetId assetId, uint32_t width, uint32_t height, TextureFormat format,
        std::span<const uint8_t> pixels, std::string& error)
    {
        if (!assetId.IsValid() || !device || !context)
        {
            error = "RegisterTexture requires a valid asset ID and initialized renderer";
            return {};
        }
        if (const TextureHandle existing = FindTexture(assetId); existing.IsValid())
        {
            error.clear();
            return existing;
        }
        RegisteredTextures.erase(assetId.value);
        if (const char* validationError = ValidateTextureData(width, height, format, pixels.size()))
        {
            error = validationError;
            return {};
        }
        if (width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        {
            error = "Texture dimensions exceed D3D11 Texture2D limits";
            return {};
        }
        DX11TextureResource texture;
        if (!CreateTextureResource(device, context, width, height, pixels.data(), texture, error)) return {};
        static uint64_t nextHandle = 1; // Never reuse handles after removal/reinitialize.
        if (nextHandle == 0)
        {
            error = "Texture handle space exhausted";
            return {};
        }
        const TextureHandle handle{ nextHandle++ };
        Textures.emplace(handle.Value, std::move(texture));
        RegisteredTextures.emplace(assetId.value, handle);
        error.clear();
        return handle;
    }

    TextureHandle DX11ResourceCache::FindTexture(TextureAssetId assetId) const
    {
        if (!assetId.IsValid()) return {};
        const auto found = RegisteredTextures.find(assetId.value);
        return found != RegisteredTextures.end() && IsTextureValid(found->second) ? found->second : TextureHandle{};
    }

    const DX11TextureResource* DX11ResourceCache::GetTexture(TextureHandle texture) const
    {
        const auto found = Textures.find(texture.Value);
        return found != Textures.end() ? &found->second : nullptr;
    }

    bool DX11ResourceCache::IsTextureValid(TextureHandle texture) const
    {
        return texture.IsValid() && GetTexture(texture) != nullptr;
    }

    void DX11ResourceCache::DestroyTexture(TextureHandle texture)
    {
        // Invalidate dependent material snapshots so the Engine renderer can rebuild them.
        std::erase_if(RegisteredMaterials, [&](const auto& entry)
        {
            const auto material = Materials.find(entry.second.Value);
            return material != Materials.end() && material->second.BaseColorTexture == texture;
        });
        std::erase_if(Materials, [texture](const auto& entry) { return entry.second.BaseColorTexture == texture; });
        std::erase_if(RegisteredTextures, [texture](const auto& entry) { return entry.second == texture; });
        Textures.erase(texture.Value);
    }

    MaterialHandle DX11ResourceCache::RegisterMaterial(MaterialAssetId assetId, const MaterialDesc& desc,
        TextureHandle baseColorTexture, std::string& error)
    {
        if (!assetId.IsValid())
        {
            error = "RegisterMaterial requires a valid asset ID and initialized renderer";
            return {};
        }
        if (const MaterialHandle existing = FindMaterial(assetId); existing.IsValid())
        {
            error.clear();
            return existing; // First registration wins until explicit removal/reset.
        }
        const Vector4& color = desc.baseColor;
        if (!std::isfinite(color.X) || !std::isfinite(color.Y) || !std::isfinite(color.Z) || !std::isfinite(color.W))
        {
            error = "RegisterMaterial requires a finite BaseColor";
            return {};
        }
        if ((desc.alphaMode != AlphaMode::Opaque && desc.alphaMode != AlphaMode::Masked && desc.alphaMode != AlphaMode::Transparent)
            || !std::isfinite(desc.alphaCutoff) || desc.alphaCutoff < 0.0f || desc.alphaCutoff > 1.0f)
        {
            error = "RegisterMaterial requires a supported AlphaMode and finite AlphaCutoff in [0, 1]";
            return {};
        }
        static uint64_t nextHandle = 1; // Never reuse a removed/pre-reset handle during this process.
        if (nextHandle == 0)
        {
            error = "Material handle space exhausted";
            return {};
        }
        const MaterialHandle handle{ nextHandle++ };
        RegisteredMaterials.erase(assetId.value);
        Materials.emplace(handle.Value, DX11MaterialResource{ desc, baseColorTexture });
        RegisteredMaterials.emplace(assetId.value, handle);
        error.clear();
        return handle;
    }

    MaterialHandle DX11ResourceCache::FindMaterial(MaterialAssetId assetId) const
    {
        if (!assetId.IsValid()) return {};
        const auto found = RegisteredMaterials.find(assetId.value);
        return found != RegisteredMaterials.end() && IsMaterialValid(found->second) ? found->second : MaterialHandle{};
    }

    const DX11MaterialResource* DX11ResourceCache::GetMaterial(MaterialHandle material) const
    {
        const auto found = Materials.find(material.Value);
        return found != Materials.end() ? &found->second : nullptr;
    }

    bool DX11ResourceCache::IsMaterialValid(MaterialHandle material) const
    {
        return material.IsValid() && GetMaterial(material) != nullptr;
    }

    void DX11ResourceCache::DestroyMaterial(MaterialHandle material)
    {
        std::erase_if(RegisteredMaterials, [material](const auto& entry) { return entry.second == material; });
        Materials.erase(material.Value);
    }

    bool DX11ResourceCache::CreateWhiteTexture(ID3D11Device* device, ID3D11DeviceContext* context, std::string& error)
    {
        if (!device || !context)
        {
            error = "CreateWhiteTexture requires an initialized renderer";
            return false;
        }
        constexpr uint8_t white[] = { 255, 255, 255, 255 };
        return CreateTextureResource(device, context, 1, 1, white, WhiteTexture, error);
    }

    ID3D11ShaderResourceView* DX11ResourceCache::GetWhiteTextureView() const
    {
        return WhiteTexture.ShaderResourceView.Get();
    }
}
