#pragma once

#include "DX11Renderer/Data/MaterialDesc.h"
#include "DX11Renderer/Data/MeshData.h"
#include "DX11Renderer/Data/RendererDebugSettings.h"
#include "DX11Renderer/Data/RendererDebugStats.h"
#include "DX11Renderer/Data/TextureData.h"
#include "DX11Renderer/Resource/DX11ResourceId.h"
#include "DX11Renderer/Scene/RenderScene.h"

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

        DX11Renderer();
        ~DX11Renderer();

        DX11Renderer(const DX11Renderer&) = delete;
        DX11Renderer& operator=(const DX11Renderer&) = delete;

        bool Initialize(void* nativeWindow, uint32_t width, uint32_t height);
        void Shutdown();

        MeshResourceId CreateMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices);
        MeshResourceId CreateMesh(std::span<const StaticVertex> vertices, std::span<const uint32_t> indices,
            std::span<const SubMesh> submeshes);
        void DestroyMesh(MeshResourceId mesh);
        bool IsMeshValid(MeshResourceId mesh) const;

        // Creation copies the descriptor once. Destroy/re-create to apply CPU edits.
        MaterialResourceId CreateMaterial(const MaterialDesc& desc, TextureResourceId baseColorTexture = {});
        // Invalid/stale IDs resolve to a default white descriptor.
        MaterialDesc GetMaterial(MaterialResourceId material) const;
        void DestroyMaterial(MaterialResourceId material);
        bool IsMaterialValid(MaterialResourceId material) const;

        TextureResourceId CreateTexture(uint32_t width, uint32_t height, TextureFormat format, std::span<const uint8_t> pixels);
        bool IsTextureValid(TextureResourceId texture) const;
        // Explicit texture removal also invalidates materials referencing it; CPU assets can re-create them.
        void DestroyTexture(TextureResourceId texture);

        void SetCamera(const float* view, const float* projection);
        void SetDebugSettings(const RendererDebugSettings& settings);
        RendererDebugStats GetDebugStats() const;

        // Queue for the next DrawScene(). Cleared after each draw.
        void SetRenderView(const RenderView& view);
        void AddRenderItem(const RenderItem& item);

        bool BeginFrame();
        // Draws the queued scene, then clears it.
        void DrawScene();
        // Consume synchronously between BeginFrame and EndFrame.
        void DrawScene(const RenderScene& scene);
        void DrawMesh(MeshResourceId mesh, const float* world, uint32_t submeshIndex = 0, MaterialResourceId material = {});
        void EndFrame();

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;

        const std::string& GetLastError() const;

    private:
        MeshResourceId CreateMeshData(const void* vertices, size_t vertexCount, uint32_t stride, VertexFormat format,
            std::span<const uint32_t> indices, std::span<const SubMesh> submeshes);
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
