#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "DX11Renderer/DX11Renderer.h"
#include "DX11Renderer/Frame/DX11ScenePreparation.h"
#include "DX11Renderer/Pipeline/DX11MeshDraw.h"
#if CANAPE_RENDER_DEBUG
#include "DX11Renderer/Pipeline/GPUProfiler.h"
#endif
#include <Windows.h>
#include <wrl/client.h>
#include <sstream>

namespace Canape
{
    using Microsoft::WRL::ComPtr;
    namespace SC = ShaderConstants;
    inline std::string RendererHResultMessage(const char* operation, HRESULT result)
    {
        std::ostringstream stream;
        stream << operation << " failed (HRESULT 0x" << std::hex
               << static_cast<unsigned long>(result) << ')';
        return stream.str();
    }

    // Existing private PImpl: owns resources; passes receive only narrow borrowed views.
    struct DX11Renderer::Impl
    {
        struct ShadowMapResources
        {
            ComPtr<ID3D11Texture2D> Texture;
            ComPtr<ID3D11DepthStencilView> DepthView;
            ComPtr<ID3D11ShaderResourceView> ShaderView;
            ComPtr<ID3D11SamplerState> PointSampler;
            ComPtr<ID3D11SamplerState> PcfSampler;
            ComPtr<ID3D11Buffer> Constants;
            ComPtr<ID3D11Buffer> PcssConstants;
            uint32_t Resolution = 0;
        };

        HWND Window = nullptr;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t BackBufferWidth = 0;
        uint32_t BackBufferHeight = 0;
        uint32_t RequestedSceneWidth = 0;
        uint32_t RequestedSceneHeight = 0;
        std::function<void()> Overlay;
        bool FrameActive = false;
        std::string LastError;
#if CANAPE_RENDER_DEBUG
        RendererDebugSettings DebugSettings{};
        RendererDebugStats CurrentStats{};
        RendererDebugStats CompletedStats{};
        uint64_t NextFrameId = 0;
        GPUProfiler Profiler;
        bool ProfilerAvailable = false;
        uint64_t MinGpuFrameId = 0;
#endif

        SC::ViewConstants ViewData{};
        SC::SceneConstants SceneData{};
        bool ViewDirty = true;
        bool SceneDirty = true;
        RenderScene QueuedScene;
        DirectionalShadowSettings ShadowSettings{};
        bool ShadowActive = false;
        uint32_t AttemptedShadowResolution = UINT32_MAX;

        ComPtr<ID3D11Device> Device;
        ComPtr<ID3D11DeviceContext> Context;
        // Reverse member destruction releases cached resources before Context and Device.
        // The cache retains neither pointer; all creation calls borrow this renderer's pair.
        DX11ResourceCache Resources;
        ComPtr<ID3D11Buffer> SkyboxVertexBuffer;
        ComPtr<ID3D11Buffer> SkyboxIndexBuffer;
        static constexpr UINT SkyboxIndexCount = 6;
        DX11PipelineState Pipelines;
        ShadowMapResources ShadowMap;
        ComPtr<ID3D11SamplerState> FallbackShadowSampler;
        ComPtr<IDXGISwapChain> SwapChain;
        ComPtr<ID3D11RenderTargetView> RenderTargetView;
        ComPtr<ID3D11RenderTargetView> OverlayRenderTargetView;
        ComPtr<ID3D11Texture2D> SceneColorTexture;
        ComPtr<ID3D11RenderTargetView> SceneColorRenderTargetView;
        ComPtr<ID3D11ShaderResourceView> SceneColorShaderResourceView;
        ComPtr<ID3D11Texture2D> DisplayTexture;
        ComPtr<ID3D11RenderTargetView> DisplayRenderTargetView;
        ComPtr<ID3D11ShaderResourceView> DisplayShaderResourceView;
        ComPtr<ID3D11ShaderResourceView> RetiredDisplayShaderResourceView;
        ComPtr<ID3D11Texture2D> DepthTexture;
        ComPtr<ID3D11DepthStencilView> DepthStencilView;
#if CANAPE_RENDER_DEBUG
        ComPtr<ID3D11ShaderResourceView> DepthShaderResourceView;
#endif
        ComPtr<ID3D11Buffer> ObjectBuffer;
        ComPtr<ID3D11Buffer> MaterialBuffer;
        ComPtr<ID3D11Buffer> ViewBuffer;
        ComPtr<ID3D11Buffer> SkyboxViewBuffer;
        ComPtr<ID3D11Buffer> SceneBuffer;
        ComPtr<ID3D11Buffer> LightBuffer;
#if CANAPE_RENDER_DEBUG
        ComPtr<ID3D11Buffer> DepthDisplayBuffer;
#endif

        Impl();
        ~Impl();
        bool EnsureShadowResources();
        PreparedLighting ResolveLighting(std::span<const RenderLight> lights, bool allowShadow);
        SceneConstantBuffers GetSceneBuffers() const;
        void UploadPendingConstants();
        void BindMainTargets();
        MainTargets GetMainTargets() const;
        SceneBindings GetSceneBindings() const;
        MeshDrawContext GetDrawContext();
        void RenderShadowPass(const PreparedScene& scene);
        void BindSharedConstants();
        void BindPipeline(const DX11PipelineSelection& pipeline);
        void DrawSkybox(const PreparedSkybox& skybox);
        void RenderFinalOutput(ID3D11RenderTargetView* target, uint32_t width, uint32_t height);
        void RenderOverlay();
        bool CreateTargets(uint32_t width, uint32_t height);
        bool CreateBackBufferTargets(uint32_t width, uint32_t height);
        bool CreateSceneTargets(uint32_t width, uint32_t height);
        bool ResizeIfNeeded(uint32_t width, uint32_t height);
        bool CreateSkyboxQuad();
        bool CreatePipeline();
        bool BeginFrame();
        void ExecuteScene(const RenderScene& scene);
        void DrawMesh(MeshResourceId mesh, const float* world, uint32_t submeshIndex, MaterialResourceId material);
        void EndFrame();
    };
}
