#pragma once

#include "Asset/AssetGpuUploader.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Canape
{
    class AssetManager;
    class DX11Renderer;
    class Scene;

    // Engine-facing render layer. Collects a Scene into a RenderScene and submits the frame.
    // Assets are uploaded and released by AssetManager through AssetGpuUploader; rendering only
    // looks up what is already uploaded. Backend types stay out of this header.
    class Renderer final : public AssetGpuUploader
    {
    public:
        Renderer();
        ~Renderer() override;

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        bool Initialize(void* nativeWindow, uint32_t width, uint32_t height);

        // A null scene or missing main camera still clears and presents the frame.
        void Render(Scene* scene, const AssetManager& assets);

        bool UploadMesh(const MeshAsset& mesh, std::string& error) override;
        bool UploadTexture(const TextureAsset& texture, std::string& error) override;
        bool UploadMaterial(const MaterialAsset& material, std::string& error) override;
        void ReleaseMeshes(std::span<const MeshAssetId> meshes) override;
        void ReleaseTextures(std::span<const TextureAssetId> textures) override;
        void ReleaseMaterials(std::span<const MaterialAssetId> materials) override;

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;

        const std::string& GetLastError() const;

    private:
        std::unique_ptr<DX11Renderer> m_Backend;
        bool m_WarnedNoCamera = false;
    };
}
