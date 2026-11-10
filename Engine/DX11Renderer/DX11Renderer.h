#pragma once

#include "DX11Renderer/Data/MaterialDesc.h"
#include "DX11Renderer/Data/MeshData.h"
#include "DX11Renderer/Data/RendererDebugSettings.h"
#include "DX11Renderer/Data/RendererDebugStats.h"
#include "DX11Renderer/Data/ShadowFilterSettings.h"
#include "DX11Renderer/Data/TextureData.h"
#include "DX11Renderer/Resource/DX11ResourceId.h"
#include "DX11Renderer/Scene/RenderScene.h"

#include <cstdint>
#include <functional>
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
        MaterialResourceId CreateMaterial(const MaterialDesc& desc, TextureResourceId baseColorTexture = {},
            TextureResourceId ormTexture = {});
        // Invalid/stale IDs resolve to a default white descriptor.
        MaterialDesc GetMaterial(MaterialResourceId material) const;
        void DestroyMaterial(MaterialResourceId material);
        bool IsMaterialValid(MaterialResourceId material) const;

        TextureResourceId CreateTexture(uint32_t width, uint32_t height, TextureFormat format, std::span<const uint8_t> pixels);
        void SetTestNormalTexture(TextureResourceId texture);
        bool IsTextureValid(TextureResourceId texture) const;
        // Explicit texture removal also invalidates materials referencing it; CPU assets can re-create them.
        void DestroyTexture(TextureResourceId texture);

        CubemapResourceId CreateCubemap(uint32_t faceSize, uint32_t mipLevels, TextureFormat format, std::span<const uint8_t> pixels);
        bool IsCubemapValid(CubemapResourceId cubemap) const;
        void DestroyCubemap(CubemapResourceId cubemap);

        void SetCamera(const float* view, const float* projection);
        // Development defaults; RenderView.postProcess is reserved for future engine integration.
        void SetPostProcessSettings(const PostProcessSettings& settings);
        PostProcessSettings GetPostProcessSettings() const;
        void SetDebugSettings(const RendererDebugSettings& settings);
        RendererDebugSettings GetDebugSettings() const;
        RendererDebugStats GetDebugStats() const;
        bool SetShadowFilterSettings(const ShadowFilterSettings& settings);
        ShadowFilterSettings GetShadowFilterSettings() const;

        // Queue for the next DrawScene(). Cleared after each draw.
        void SetRenderView(const RenderView& view);
        void AddRenderItem(const RenderItem& item);
        void AddLight(const RenderLight& light);
        void SetFog(const RenderFog& fog);
        void SetSkybox(const RenderSkybox& skybox);
        void SetAmbient(const Vector3& color, float intensity);

        bool BeginFrame();
        // Draws the queued scene, then clears it.
        void DrawScene();
        // Consume synchronously between BeginFrame and EndFrame.
        void DrawScene(const RenderScene& scene);
        void DrawMesh(MeshResourceId mesh, const float* world, uint32_t submeshIndex = 0, MaterialResourceId material = {});
        void EndFrame();

        void SetSceneSize(uint32_t width, uint32_t height);
        void SetOverlay(std::function<void()> overlay);
        // Called inside DrawScene with the scene targets, the viewport and the shared constants bound.
        // The callee keeps the triangle-list topology, uses only b9, t8-t10, s8 and u0-u1 and unbinds them.
        void SetAfterOpaqueCallback(std::function<void()>);
        void SetAfterTransparentCallback(std::function<void()>);

        void* GetNativeDevice() const;
        void* GetNativeContext() const;
        void* GetSceneTexture() const;

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;

        const std::string& GetLastError() const;

    private:
        MeshResourceId CreateMeshData(const void* vertices, size_t vertexCount, uint32_t stride, VertexFormat format,
            std::span<const uint32_t> indices, std::span<const SubMesh> submeshes);
        struct Impl;
        // Present from construction through destruction; Shutdown only clears its state.
        std::unique_ptr<Impl> m_Impl;
    };
}
