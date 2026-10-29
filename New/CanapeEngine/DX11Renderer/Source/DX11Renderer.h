#pragma once

#include "DX11ResourceManager.h"
#include "../DX11RenderItem.h"

#include <cstdint>
#include <span>
#include <string>
#include <memory>

namespace Canape
{
    struct DX11SubMesh;
    struct DX11MaterialDesc;
    class Device;
    class DX11SwapChain;
    class DX11RenderScene;
    class DX11Shader;
    class DX11RenderState;

    class DX11Renderer
    {
    public:
        DX11Renderer();
        ~DX11Renderer();

    public:
        bool UpLoadMeshResource();

        MeshResourceId CreateMeshResource(const void* vertexData, uint32_t vertexCount, uint32_t vertexStride,
            std::span<const uint32_t> indices, std::span<const DX11SubMesh> subMeshes);
        TextureResourceId CreateTextureResource(uint32_t width, uint32_t height, std::span<const uint8_t> rgba8Pixels);
        MaterialResourceId CreateMaterialResource(const DX11MaterialDesc& desc, TextureResourceId baseColorTexture);

        void DestroyMeshResource(MeshResourceId id);
        void DestroyTextureResource(TextureResourceId id);
        void DestroyMaterialResource(MaterialResourceId id);

        void SetCamera(const DX11RenderView& camera);
        void AddRenderItem(const DX11RenderItem& item);

    public:
        bool Initialize(void* nativeWindow, uint32_t width, uint32_t height);
        bool BeginFrame();
        void DrawScene();
        void EndFrame();

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;
        const std::string& GetLastError() const;

    private:
        uint32_t m_Width = 1;
        uint32_t m_Height = 1;
        std::string m_LastError;

    private:
        std::unique_ptr<Device> m_device;
        std::unique_ptr<DX11SwapChain> m_swapChain;
        std::unique_ptr<DX11ResourceManager> m_resourceManager;
        std::unique_ptr<DX11RenderScene> m_renderScene;
        std::unique_ptr<DX11Shader> m_shader;
        std::unique_ptr<DX11RenderState> m_renderState;
        TextureResourceId m_whiteTexture;
    };
}
