#pragma once

#include "DX11Renderer/Data/EditorGridPlane.h"
#include "DX11Renderer/Data/MaterialDesc.h"
#include "DX11Renderer/Data/MeshData.h"
#include "DX11Renderer/Data/RendererDebugSettings.h"
#include "DX11Renderer/Data/RendererDebugStats.h"
#include "DX11Renderer/Data/ShadowFilterSettings.h"
#include "DX11Renderer/Data/TextureData.h"
#include "DX11Renderer/Data/UIDrawList.h"
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

        bool Initialize(void* nativeWindow, uint32_t width, uint32_t height, bool headless);
        void Shutdown();

        MeshResourceId CreateMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices);
        MeshResourceId CreateMesh(std::span<const StaticVertex> vertices, std::span<const uint32_t> indices,
            std::span<const SubMesh> submeshes);
        MeshResourceId CreateMesh(std::span<const SkinnedVertex> vertices, std::span<const uint32_t> indices,
            std::span<const SubMesh> submeshes);
        void DestroyMesh(MeshResourceId mesh);
        bool IsMeshValid(MeshResourceId mesh) const;

        // Creation copies the descriptor once. Destroy/re-create to apply CPU edits.
        // Normal and occlusion participate in shading; emissive is stored for future shading support.
        MaterialResourceId CreateMaterial(const MaterialDesc& desc, TextureResourceId baseColorTexture = {},
            TextureResourceId ormTexture = {}, TextureResourceId normalTexture = {},
            TextureResourceId occlusionTexture = {}, TextureResourceId emissiveTexture = {});
        // Invalid/stale IDs resolve to a default white descriptor.
        MaterialDesc GetMaterial(MaterialResourceId material) const;
        void DestroyMaterial(MaterialResourceId material);
        bool IsMaterialValid(MaterialResourceId material) const;

        TextureResourceId CreateTexture(uint32_t width, uint32_t height, TextureFormat format, std::span<const uint8_t> pixels);
        bool IsTextureValid(TextureResourceId texture) const;
        // Overwrites a rectangle of mip 0 (tightly packed RGBA8) and regenerates mips, e.g. a glyph atlas.
        bool UpdateTexture(TextureResourceId texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
            std::span<const uint8_t> pixels);
        // Explicit texture removal also invalidates materials referencing it; CPU assets can re-create them.
        void DestroyTexture(TextureResourceId texture);

        CubemapResourceId CreateCubemap(uint32_t faceSize, uint32_t mipLevels, TextureFormat format, std::span<const uint8_t> pixels);
        bool IsCubemapValid(CubemapResourceId cubemap) const;
        void DestroyCubemap(CubemapResourceId cubemap);

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
        void AddRenderItem(const MeshItem& item);
        uint32_t AddBonePalette(std::span<const Matrix4> palette);
        void AddSkinnedRenderItem(const SkinnedMeshItem& item);
        void AddLight(const RenderLight& light);
        void SetFog(const RenderFog& fog);
        void SetSkybox(const RenderSkybox& skybox);
        void SetMirrorPlaneHeight(float height);
        void SetAmbient(const Vector3& color, float intensity);

        bool BeginFrame();
        // Draws the queued scene, then clears it.
        void DrawScene();
        // Consume synchronously between BeginFrame and EndFrame.
        void DrawScene(const RenderScene& scene);
        // Screen-space UI drawn by the next EndFrame after final output, before the overlay.
        // The list is borrowed: keep it alive until EndFrame returns. Cleared by EndFrame.
        void SetUIDrawList(const UIDrawList* list);
        void EndFrame();

        void SetSceneSize(uint32_t width, uint32_t height);
        void SetOverlay(std::function<void()> overlay);
        void SetEditorGridEnabled(bool enabled);
        bool IsEditorGridEnabled() const;
        void SetEditorGridPlaneVisible(EditorGridPlane plane, bool visible);
        bool IsEditorGridPlaneVisible(EditorGridPlane plane) const;
        // Called inside DrawScene with the scene targets, the viewport and the scene's shared constants bound.
        // The callee keeps the triangle-list topology, binds its own at b9-b11, t8-t10 and s8, and unbinds it.
        void SetAfterOpaqueCallback(std::function<void()>);
        void SetAfterTransparentCallback(std::function<void()>);
        // The same contract inside the mirror pass, after its opaque meshes: the mirror's targets and view constants.
        // Not called when there is no mirror view, that is no mirror plane or a camera below or barely above it.
        void SetMirrorCallback(std::function<void()>);

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
