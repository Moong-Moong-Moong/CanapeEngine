#pragma once

#include "DX11Renderer/DX11Renderer.h"
#include "DX11Renderer/Frame/DX11ScenePreparation.h"
#include "DX11Renderer/Pipeline/DX11MeshDraw.h"
#include "DX11Renderer/Pass/DX11UIPass.h"
#include "DX11Renderer/Pass/DX11IBLBaker.h"
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

        struct MirrorResources
        {
            ComPtr<ID3D11RenderTargetView> ColorView;
            ComPtr<ID3D11DepthStencilView> DepthView;
            ComPtr<ID3D11ShaderResourceView> ShaderView;
        };

        SC::ViewConstants ViewData{};
        SC::SceneConstants SceneData{};
        bool ViewDirty = true;
        bool SceneDirty = true;
        HWND Window = nullptr;
        bool Headless = false;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t BackBufferWidth = 0;
        uint32_t BackBufferHeight = 0;
        uint32_t RequestedSceneWidth = 0;
        uint32_t RequestedSceneHeight = 0;
        std::function<void()> Overlay;
        std::function<void()> AfterOpaqueCallback;
        std::function<void()> AfterTransparentCallback;
        std::function<void()> MirrorCallback;
        struct PickResources
        {
            ComPtr<ID3D11Texture2D> Texture;
            ComPtr<ID3D11RenderTargetView> Target;
            ComPtr<ID3D11Texture2D> DepthTexture;
            ComPtr<ID3D11DepthStencilView> DepthView;
            ComPtr<ID3D11Texture2D> Staging;
            ComPtr<ID3D11Buffer> Constants;
            uint32_t Width = 0;
            uint32_t Height = 0;
        } Pick;
        bool PickRequested = false;
        uint32_t PickX = 0;
        uint32_t PickY = 0;
        bool PickReadbackPending = false;
        std::vector<uint64_t> PickTable;
        bool PickResultReady = false;
        uint64_t PickResult = 0;
        bool EditorGridEnabled = false;
        uint8_t GridPlaneMask = 1u << static_cast<uint32_t>(EditorGridPlane::XZ);
        float GridCellSize = 1.0f;
        const UIDrawList* PendingUI = nullptr; // Borrowed until EndFrame.
        bool Initialized = false;
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

        RenderScene QueuedScene;
        DirectionalShadowSettings ShadowSettings{};
        PostProcessSettings postProcessSettings_{};
        bool ShadowActive = false;
        uint32_t AttemptedShadowResolution = UINT32_MAX;

        ComPtr<ID3D11Device> Device;
        ComPtr<ID3D11DeviceContext> Context;
        // Scene color, camera depth and object-ID masks use the same actual sample description.
        static constexpr UINT RequestedSceneSampleCount = 4;
        static_assert(RequestedSceneSampleCount == 1 || RequestedSceneSampleCount == 4);
        // Shared by scene rendering and camera depth; resource creation may fall back to 1x.
        DXGI_SAMPLE_DESC SelectedSceneSampleDesc{ 1, 0 };
        // Reverse member destruction releases cached resources before Context and Device.
        // The cache retains neither pointer; all creation calls borrow this renderer's pair.
        DX11ResourceCache Resources;
        ComPtr<ID3D11Buffer> SkyboxVertexBuffer;
        ComPtr<ID3D11Buffer> SkyboxIndexBuffer;
        static constexpr UINT SkyboxIndexCount = 6;
        DX11PipelineState Pipelines;
        struct IBLBakeSettings
        {
            uint32_t IrradianceSamples = 512;
            uint32_t PrefilterSamples = 256;
            uint32_t PrefilterFaceSize = DX11IBLBaker::PrefilterFaceSize;
            uint32_t PrefilterMipLevels = DX11IBLBaker::PrefilterMipLevels;
            uint32_t BRDFSamples = 256;
            uint32_t BRDFSize = DX11IBLBaker::BRDFLUTSize;
        } IBLSettings;
        struct EnvironmentIBLResources
        {
            CubemapResourceId SourceId{};
            ID3D11ShaderResourceView* SourceView = nullptr; // Borrowed from Resources.
            DX11IrradianceCubemap Irradiance;
            DX11PrefilteredCubemap Prefiltered;
            uint32_t PrefilterMipCount = 0;
        } EnvironmentIBL;
        DX11IBLBaker IBLBaker;
        DX11BRDFLUT BRDFLUT;
        bool IBLBakerAttempted = false;
        bool IBLBakerReady = false;
        CubemapResourceId FailedIBLSourceId{};
        ID3D11ShaderResourceView* FailedIBLSourceView = nullptr;
        ShadowMapResources ShadowMap;
        MirrorResources Mirror;
        ComPtr<ID3D11SamplerState> FallbackShadowSampler;
        ComPtr<IDXGISwapChain> SwapChain;
        ComPtr<ID3D11RenderTargetView> RenderTargetView;
        ComPtr<ID3D11RenderTargetView> OverlayRenderTargetView;
        ComPtr<ID3D11Texture2D> SceneRenderTexture;
        // Always single-sampled for post processing; aliases SceneRenderTexture at 1x.
        ComPtr<ID3D11Texture2D> SceneColorTexture;
        // Scene rendering RTV: always views SceneRenderTexture, not the resolve destination.
        ComPtr<ID3D11RenderTargetView> SceneColorRenderTargetView;
        ComPtr<ID3D11ShaderResourceView> SceneColorShaderResourceView;
        ComPtr<ID3D11Texture2D> DisplayTexture;
        ComPtr<ID3D11RenderTargetView> DisplayRenderTargetView;
        // UNORM view of DisplayTexture: the UI pass blends sRGB-encoded values here.
        ComPtr<ID3D11RenderTargetView> DisplayUIRenderTargetView;
        ComPtr<ID3D11ShaderResourceView> DisplayShaderResourceView;
        ComPtr<ID3D11ShaderResourceView> RetiredDisplayShaderResourceView;
        ComPtr<ID3D11Texture2D> DepthTexture;
        ComPtr<ID3D11DepthStencilView> DepthStencilView;
        ComPtr<ID3D11Texture2D> OutlineMaskTexture;
        ComPtr<ID3D11RenderTargetView> OutlineMaskTarget;
        ComPtr<ID3D11ShaderResourceView> OutlineMaskView;
        ComPtr<ID3D11Buffer> OutlineMaskBuffer;
        ComPtr<ID3D11Buffer> OutlineCompositeBuffer;
        ComPtr<ID3D11Buffer> OutlineStyleBuffer;
        ComPtr<ID3D11ShaderResourceView> OutlineStyleView;
        uint32_t OutlineStyleCapacity = 0;
        uint32_t OutlineStyleCount = 0;
        uint32_t OutlineMaxThickness = 0;
        bool OutlineMaskReady = false;
        // Camera depth as a texture (Texture2DMS when MSAA). Read by the world-space UI depth
        // comparison in every configuration and by the debug depth view.
        ComPtr<ID3D11ShaderResourceView> DepthShaderResourceView;
        ComPtr<ID3D11Buffer> ObjectBuffer;
        ComPtr<ID3D11Buffer> SkinningBuffer;
        ComPtr<ID3D11Buffer> MaterialBuffer;
        ComPtr<ID3D11Buffer> ViewBuffer;
        ComPtr<ID3D11Buffer> MirrorViewBuffer;
        ComPtr<ID3D11Buffer> SkyboxViewBuffer;
        ComPtr<ID3D11Buffer> IBLBuffer;
        ComPtr<ID3D11Buffer> SceneBuffer;
        ComPtr<ID3D11Buffer> LightBuffer;
        ComPtr<ID3D11Buffer> PostProcessBuffer;
        ComPtr<ID3D11Buffer> EditorGridBuffer;
        DX11UIBuffers UIBuffers;
#if CANAPE_RENDER_DEBUG
        ComPtr<ID3D11Buffer> DepthDisplayBuffer;
#endif

        Impl();
        ~Impl();
        void Shutdown();
        void InitializeSceneSampleDesc();
#if CANAPE_RENDER_DEBUG
        void InitializeProfiler();
        void BeginFrameProfiling(ID3D11DeviceContext* context);
#endif
        bool EnsureShadowResources();
        PreparedLighting ResolveLighting(std::span<const RenderLight> lights, bool allowShadow);
        SceneConstantBuffers GetSceneBuffers() const;
        void UploadPendingConstants();
        void BindMainTargets();
        MainTargets GetMainTargets() const;
        SceneBindings GetSceneBindings() const;
        void ClearEnvironmentIBL();
        void EnsureSceneIBL(CubemapResourceId cubemap, ID3D11ShaderResourceView* source);
        void InvalidateIBL(CubemapResourceId cubemap);
        MeshDrawContext GetDrawContext();
        void RenderShadowPass(const PreparedScene& scene);
        void RenderMirrorPass(const PreparedScene& scene, MeshDrawContext draw, float planeHeight);
        void BindSharedConstants();
        void BindPipeline(const DX11PipelineSelection& pipeline);
        void DrawSkybox(const PreparedSkybox& skybox);
        void DrawEditorGrid();
        bool EnsurePickResources();
        void ExecutePickPass(const PreparedScene& scene, MeshDrawContext& draw);
        void PollPickResult();
        void SetPostProcessSettings(const PostProcessSettings& settings);
        void ResolveSceneColor();
        void ExecutePostProcessing(ID3D11RenderTargetView* target, uint32_t width, uint32_t height);
        void ExecuteFinalCompositePass(ID3D11RenderTargetView* target, uint32_t width, uint32_t height);
        bool EnsureOutlineResources();
        bool UploadOutlineStyles(const PreparedScene& scene);
        void ReleaseOutlineResources();
        bool OutlinesAllowed() const;
        void ExecuteOutlineComposite(ID3D11RenderTargetView* target, uint32_t width, uint32_t height);
        void ExecuteUIPass(ID3D11RenderTargetView* target, uint32_t width, uint32_t height);
        void RenderOverlay();
        bool CreateTargets(uint32_t width, uint32_t height);
        bool CreateBackBufferTargets(uint32_t width, uint32_t height);
        bool CreateSceneTargets(uint32_t width, uint32_t height);
        bool ResizeIfNeeded(uint32_t width, uint32_t height);
        bool CreateSkyboxQuad();
        bool CreatePipeline();
        bool BeginFrame();
        void ExecuteScene(const RenderScene& scene);
        void EndFrame();

    private:
        void recordDebugStat() {
#if CANAPE_RENDER_DEBUG
            CompletedStats = CurrentStats;
#endif
        }
    };
}
