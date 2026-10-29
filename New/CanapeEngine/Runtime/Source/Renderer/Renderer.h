#pragma once

#include "Asset/AssetId.h"
#include "Math/Matrix4.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Canape
{
    class AssetManager;
    class DX11Renderer;
    class GpuResourceRegistry;
    class Scene;

    class Renderer final
    {
    public:
        Renderer();
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        bool Initialize(void* nativeWindow, uint32_t width, uint32_t height);

        void Render(Scene* scene, const AssetManager& assets);

        void AddRenderItem(MeshAssetId mesh, uint32_t submeshIndex, MaterialAssetId material, const Matrix4& world);

        GpuResourceRegistry& GetRegistry() { return *m_Registry; }

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;

        const std::string& GetLastError() const;

    private:
        std::unique_ptr<DX11Renderer> m_DX11;
        std::unique_ptr<GpuResourceRegistry> m_Registry;
        bool m_WarnedNoCamera = false;
    };
}
