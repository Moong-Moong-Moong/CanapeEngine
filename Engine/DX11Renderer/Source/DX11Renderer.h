#pragma once

#include "MeshHandle.h"
#include "MeshAssetId.h"
#include "RenderFrame.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace Canape
{
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
        MeshHandle FindMesh(MeshAssetId assetId) const;
        void DestroyMesh(MeshHandle mesh);
        bool IsMeshValid(MeshHandle mesh) const;

        void SetCamera(const float* view, const float* projection);

        bool BeginFrame();
        // Consume synchronously between BeginFrame and EndFrame.
        void DrawFrame(const RenderFrame& frame);
        void DrawMesh(MeshHandle mesh, const float* world);
        void EndFrame();

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;

        const std::string& GetLastError() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
