#pragma once

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

        using MeshHandle = uint32_t;
        static constexpr MeshHandle InvalidMesh = 0;

        DX11Renderer();
        ~DX11Renderer();

        DX11Renderer(const DX11Renderer&) = delete;
        DX11Renderer& operator=(const DX11Renderer&) = delete;

        bool Initialize(void* nativeWindow, uint32_t width, uint32_t height);
        void Shutdown();

        MeshHandle CreateMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices);
        void DestroyMesh(MeshHandle mesh);

        void SetCamera(const float* view, const float* projection);

        bool BeginFrame();
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
