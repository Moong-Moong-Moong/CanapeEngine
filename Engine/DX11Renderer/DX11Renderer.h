#pragma once

#include "Asset/AssetId.h"
#include "Asset/Material/MaterialDesc.h"
#include "Asset/Mesh/MeshData.h"
#include "Asset/Texture/TextureData.h"
#include "Renderer/Handle/MaterialHandle.h"
#include "Renderer/Handle/MeshHandle.h"
#include "Renderer/Handle/TextureHandle.h"
#include "Renderer/RenderScene.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace Canape
{
    // DX11 backend. Engine code reaches it only through Renderer; D3D types stay in the .cpp.
    class DX11Renderer
    {
    public:
        struct Vertex
        {
            float Position[3];
        };

        using MeshHandle = Canape::MeshHandle;
        static constexpr MeshHandle InvalidMesh{};

        DX11Renderer();
        ~DX11Renderer();

        DX11Renderer(const DX11Renderer&) = delete;
        DX11Renderer& operator=(const DX11Renderer&) = delete;

        bool Initialize(void* nativeWindow, uint32_t width, uint32_t height);
        void Shutdown();

        MeshHandle CreateMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices);
        MeshHandle RegisterMesh(MeshAssetId assetId, std::span<const Vertex> vertices, std::span<const uint32_t> indices);
        MeshHandle CreateMesh(std::span<const StaticVertex> vertices, std::span<const uint32_t> indices,
            std::span<const SubMesh> submeshes);
        MeshHandle RegisterMesh(MeshAssetId assetId, std::span<const StaticVertex> vertices,
            std::span<const uint32_t> indices, std::span<const SubMesh> submeshes);
        MeshHandle FindMesh(MeshAssetId assetId) const;
        void DestroyMesh(MeshHandle mesh);
        bool IsMeshValid(MeshHandle mesh) const;

        // Registration copies the descriptor once. Remove/re-register to apply CPU edits.
        MaterialHandle RegisterMaterial(MaterialAssetId assetId, const MaterialDesc& desc, TextureHandle baseColorTexture = {});
        MaterialHandle FindMaterial(MaterialAssetId assetId) const;
        // Invalid/stale handles resolve to a default white descriptor.
        MaterialDesc GetMaterial(MaterialHandle material) const;
        void DestroyMaterial(MaterialHandle material);
        bool IsMaterialValid(MaterialHandle material) const;

        TextureHandle RegisterTexture(TextureAssetId assetId, uint32_t width, uint32_t height,
            TextureFormat format, std::span<const uint8_t> pixels);
        TextureHandle FindTexture(TextureAssetId assetId) const;
        bool IsTextureValid(TextureHandle texture) const;
        // Explicit texture removal also invalidates materials referencing it; CPU assets can re-register.
        void DestroyTexture(TextureHandle texture);

        void SetCamera(const float* view, const float* projection);

        bool BeginFrame();
        // Consume synchronously between BeginFrame and EndFrame.
        void DrawScene(const RenderScene& scene);
        void DrawMesh(MeshHandle mesh, const float* world, uint32_t submeshIndex = 0, MaterialHandle material = {});
        void EndFrame();

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;

        const std::string& GetLastError() const;

    private:
        MeshHandle CreateMeshData(const void* vertices, size_t vertexCount, uint32_t stride, VertexFormat format,
            std::span<const uint32_t> indices, std::span<const SubMesh> submeshes);
        MeshHandle RegisterMeshData(MeshAssetId assetId, const void* vertices, size_t vertexCount,
            uint32_t stride, VertexFormat format, std::span<const uint32_t> indices, std::span<const SubMesh> submeshes);
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
