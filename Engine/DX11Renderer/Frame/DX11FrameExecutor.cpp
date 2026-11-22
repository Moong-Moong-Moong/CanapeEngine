#include "DX11Renderer/Internal/DX11RendererImpl.h"
#include "DX11Renderer/Pass/DX11ShadowPass.h"
#include "DX11Renderer/Pass/DX11MainPass.h"
#include "DX11Renderer/Pass/DX11SkyboxPass.h"
#include "DX11Renderer/Pass/DX11FinalOutputPass.h"
#include "CNDebug/DebugLog.h"
#include <utility>

namespace Canape
{
    namespace
    {
        // Linear RGB and alpha are stored directly in the FP16 scene rendering target.
        constexpr float SceneClearColor[] = { 0.025f, 0.035f, 0.055f, 1.0f };
    }

    void DX11Renderer::Impl::ClearEnvironmentIBL()
    {
        if (Context)
        {
            ID3D11ShaderResourceView* unbound[3]{};
            Context->PSSetShaderResources(6, 3, unbound);
        }
        EnvironmentIBL = {};
    }

    void DX11Renderer::Impl::InvalidateIBL(CubemapResourceId cubemap)
    {
        if (EnvironmentIBL.SourceId == cubemap) ClearEnvironmentIBL();
        if (FailedIBLSourceId == cubemap)
        {
            FailedIBLSourceId = {};
            FailedIBLSourceView = nullptr;
        }
    }

    void DX11Renderer::Impl::EnsureSceneIBL(CubemapResourceId cubemap,
        ID3D11ShaderResourceView* source)
    {
        if (!cubemap.IsValid() || !source)
        {
            ClearEnvironmentIBL();
            FailedIBLSourceId = {};
            FailedIBLSourceView = nullptr;
            return;
        }
        if (EnvironmentIBL.SourceId == cubemap && EnvironmentIBL.SourceView == source
            && EnvironmentIBL.Irradiance.ShaderResourceView
            && EnvironmentIBL.Prefiltered.ShaderResourceView && BRDFLUT.ShaderResourceView)
            return;
        ClearEnvironmentIBL(); // Single-environment cache; never expose the previous HDRI.
        if (FailedIBLSourceId == cubemap && FailedIBLSourceView == source) return;
        if (!IBLBakerAttempted)
        {
            IBLBakerAttempted = true;
            std::string error;
            IBLBakerReady = IBLBaker.Initialize(Device.Get(), Pipelines.ResolveFinalOutput(false).VS, error);
            if (!IBLBakerReady)
            {
                LastError = "IBL baker initialization: " + error;
                CN_LOG_RENDERER("IBL", LastError);
                return;
            }
        }
        if (!IBLBakerReady) return;

        EnvironmentIBLResources pending;
        pending.SourceId = cubemap;
        pending.SourceView = source;
        DX11BRDFLUT pendingBRDF;
        std::string error;
        const bool baked = IBLBaker.BakeIrradiance(Context.Get(), source, pending.Irradiance,
                error, IBLSettings.IrradianceSamples)
            && IBLBaker.BakePrefilteredEnvironment(Context.Get(), source, pending.Prefiltered,
                error, IBLSettings.PrefilterSamples, IBLSettings.PrefilterFaceSize,
                IBLSettings.PrefilterMipLevels)
            && (BRDFLUT.ShaderResourceView || IBLBaker.BakeBRDFLUT(Context.Get(), pendingBRDF,
                error, IBLSettings.BRDFSamples, IBLSettings.BRDFSize));
        if (!baked)
        {
            FailedIBLSourceId = cubemap;
            FailedIBLSourceView = source;
            LastError = "IBL bake: " + error;
            CN_LOG_RENDERER("IBL", LastError);
            return;
        }
        pending.PrefilterMipCount = IBLSettings.PrefilterMipLevels;
        EnvironmentIBL = std::move(pending);
        if (pendingBRDF.ShaderResourceView) BRDFLUT = std::move(pendingBRDF);
        FailedIBLSourceId = {};
        FailedIBLSourceView = nullptr;
    }

    // The executor is a module of existing Impl members, not another state owner.
    PreparedLighting DX11Renderer::Impl::ResolveLighting(std::span<const RenderLight> lights, bool allowShadow)
    {
        auto data = ScenePreparation::PrepareLights(lights, ShadowSettings);
        if (allowShadow && ShadowSettings.enabled && data.ShadowDirectionalIndex != SC::InvalidShadowView
            && Pipelines.ShadowsAvailable() && EnsureShadowResources())
        {
            ScenePreparation::PrepareShadow(data, ViewData, ShadowSettings, ShadowMap.Resolution);
        }
        ShadowActive = data.ShadowActive;
        return data;
    }

    void DX11Renderer::Impl::UploadPendingConstants()
    {
        ConstantBuffers::UploadPending(*Context.Get(), GetSceneBuffers(), ViewData, SceneData,
            ViewDirty, SceneDirty);
    }

    void DX11Renderer::Impl::RenderShadowPass(const PreparedScene& scene)
    {
        auto draw = GetDrawContext();
        draw.BonePalettes = std::span<const Matrix4>{ scene.BonePalettes };
        ShadowPass::Execute(scene, draw, { ShadowMap.DepthView.Get(), ShadowMap.Resolution }, GetMainTargets());
    }

    void DX11Renderer::Impl::RenderMirrorPass(const PreparedScene& scene, MeshDrawContext draw, float planeHeight)
    {
        ID3D11ShaderResourceView* unbound = nullptr;
        Context->PSSetShaderResources(SC::MirrorSlot, 1, &unbound);
        Context->ClearRenderTargetView(Mirror.ColorView.Get(), SceneClearColor);
        Context->ClearDepthStencilView(Mirror.DepthView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        if (const auto view = ScenePreparation::BuildMirrorViewConstants(ViewData, planeHeight))
        {
            ConstantBuffers::Upload(*Context.Get(), MirrorViewBuffer.Get(), *view);
            draw.Scene.Constants.View = MirrorViewBuffer.Get();
            RenderBindings::BindMainTargets(*Context.Get(), { Mirror.ColorView.Get(), Mirror.DepthView.Get(), Width, Height });
            RenderBindings::BindScene(*Context.Get(), draw.Scene);
            MainPass::DrawOpaqueAndMasked(scene, draw);
            if (MirrorCallback) MirrorCallback();
            BindMainTargets();
        }
        ID3D11ShaderResourceView* mirrorTexture = Mirror.ShaderView.Get();
        Context->PSSetShaderResources(SC::MirrorSlot, 1, &mirrorTexture);
    }

    void DX11Renderer::Impl::DrawSkybox(const PreparedSkybox& skybox)
    {
        if (!skybox.Texture) return;
        ConstantBuffers::Upload(*Context.Get(), SkyboxViewBuffer.Get(), skybox.Constants);
        RendererDebugStats* stats = nullptr;
#if CANAPE_RENDER_DEBUG
        stats = &CurrentStats;
#endif
        SkyboxPass::Execute(skybox, *Context.Get(), Pipelines,
            { SkyboxVertexBuffer.Get(), SkyboxIndexBuffer.Get(), SkyboxViewBuffer.Get() }, GetMainTargets(), stats);
    }

    void DX11Renderer::Impl::ExecutePostProcessing(ID3D11RenderTargetView* target, uint32_t width, uint32_t height)
    {
        // Future HDR effects that need their own textures/passes run here, before SDR conversion.
        ExecuteFinalCompositePass(target, width, height);
    }

    void DX11Renderer::Impl::ExecuteFinalCompositePass(ID3D11RenderTargetView* target, uint32_t width, uint32_t height)
    {
        ID3D11ShaderResourceView* depth = nullptr;
        ID3D11Buffer* depthDisplay = nullptr;
        const RendererDebugSettings* debug = nullptr;
        RendererDebugStats* stats = nullptr;
#if CANAPE_RENDER_DEBUG
        depth = DepthShaderResourceView.Get();
        depthDisplay = DepthDisplayBuffer.Get();
        debug = &DebugSettings;
        stats = &CurrentStats;
#endif
        // This is the SampleDesc applied to camera depth, including resource-creation fallback.
        const auto output = ScenePreparation::PrepareFinalOutput(Pipelines, SceneColorShaderResourceView.Get(),
            depth, ShadowMap.ShaderView.Get(), ShadowActive, debug, SelectedSceneSampleDesc.Count);
#if CANAPE_RENDER_DEBUG
        if (output.DisplayDepth) ConstantBuffers::Upload(*Context.Get(), depthDisplay, output.DepthDisplay);
#endif
        // Current single settings source. Future per-view integration should select
        // scene.camera.postProcess in ExecuteScene instead of these development defaults.
        SC::PostProcessConstants postProcess{};
        postProcess.PostProcessingEnabled = postProcessSettings_.enabled ? 1u : 0u;
        postProcess.ToneMappingEnabled = postProcessSettings_.toneMappingEnabled ? 1u : 0u;
#if CANAPE_RENDER_DEBUG
        postProcess.DebugViewEnabled = debug->enabled && debug->viewMode != DebugViewMode::Final ? 1u : 0u;
#endif
        ConstantBuffers::Upload(*Context.Get(), PostProcessBuffer.Get(), postProcess);
        FinalOutputPass::Execute(output, *Context.Get(), target, width, height,
            depthDisplay, PostProcessBuffer.Get(), stats);
    }

#if CANAPE_RENDER_DEBUG
    void DX11Renderer::Impl::BeginFrameProfiling(ID3D11DeviceContext* context)
    {

        CurrentStats = {};
        CurrentStats.frameId = ++NextFrameId;
        if (ProfilerAvailable)
        {
            Profiler.Poll(context);
            if (DebugSettings.captureGpuTimings)
                Profiler.BeginFrame(context, CurrentStats.frameId);
        }
    }
#endif

    bool DX11Renderer::Impl::BeginFrame()
    {
        if (!Initialized || FrameActive)
        {
            return false;
        }

        RECT clientRect{};
        if (!GetClientRect(Window, &clientRect))
        {
            return false;
        }

        const uint32_t width = static_cast<uint32_t>(clientRect.right - clientRect.left);
        const uint32_t height = static_cast<uint32_t>(clientRect.bottom - clientRect.top);
        if (width == 0 || height == 0 || !ResizeIfNeeded(width, height))
        {
            return false;
        }

        ID3D11DeviceContext* context = Context.Get();

#if CANAPE_RENDER_DEBUG
        BeginFrameProfiling(context);
#endif

        // Clear any previous final-output/depth read before writing the scene targets.
        ID3D11ShaderResourceView* unbound = nullptr;
        context->PSSetShaderResources(0, 1, &unbound);
        context->ClearRenderTargetView(SceneColorRenderTargetView.Get(), SceneClearColor);
        context->ClearDepthStencilView(DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        BindMainTargets();

        // Start with fresh light/shadow state, including frames with no scene submission.
        // Each DrawScene replaces this data with its own prepared lighting.
        const auto lighting = ResolveLighting(QueuedScene.lights, false);
        ConstantBuffers::UploadLighting(*context, GetSceneBuffers(), lighting);
        SceneData = {};
        SceneDirty = true;
        // Reset the complete default pipeline, including frames ending in transparency.
        BindPipeline(Pipelines.Resolve(VertexFormat::Position, AlphaMode::Opaque));

        FrameActive = true;
        return true;
    }

#if CANAPE_RENDER_DEBUG
    struct SceneScope {
        bool ProfilerAvailable;
        Canape::GPUProfiler& Profiler;
        ID3D11DeviceContext* Context;

        SceneScope(bool ProfilerAvailable, Canape::GPUProfiler& Profiler, ID3D11DeviceContext* Context)
            : ProfilerAvailable(ProfilerAvailable), Profiler(Profiler), Context(Context)
        {
            if (ProfilerAvailable) Profiler.BeginDrawScene(Context);
        }

        ~SceneScope() {
            if (ProfilerAvailable) Profiler.EndDrawScene(Context);
        }

        SceneScope(const SceneScope&) = delete;
        SceneScope& operator=(const SceneScope&) = delete;
        SceneScope(SceneScope&&) = delete;
        SceneScope& operator=(SceneScope&&) = delete;
    };

    struct FinalOutputScope {
        bool ProfilerAvailable;
        Canape::GPUProfiler& Profiler;
        ID3D11DeviceContext* Context;

        FinalOutputScope(bool ProfilerAvailable, Canape::GPUProfiler& Profiler, ID3D11DeviceContext* Context)
            : ProfilerAvailable(ProfilerAvailable), Profiler(Profiler), Context(Context)
        {
            if (ProfilerAvailable) Profiler.BeginFinalOutput(Context);
        }

        ~FinalOutputScope() {
            if (ProfilerAvailable)
            {
                Profiler.EndFinalOutput(Context);
                Profiler.EndFrame(Context);
            }
        }

        FinalOutputScope(const FinalOutputScope&) = delete;
        FinalOutputScope& operator=(const FinalOutputScope&) = delete;
        FinalOutputScope(FinalOutputScope&&) = delete;
        FinalOutputScope& operator=(FinalOutputScope&&) = delete;
    };
#endif

    void DX11Renderer::Impl::ExecuteScene(const RenderScene& scene)
    {
        if (!FrameActive)
        {
            return;
        }

        {
#if CANAPE_RENDER_DEBUG
            SceneScope scope(ProfilerAvailable, Profiler, Context.Get());
#endif

            const auto prepared = ScenePreparation::PrepareScene(scene, Resources, LastError);
            EnsureSceneIBL(scene.skybox.cubemap, prepared.Skybox.Texture);
            ViewData = prepared.View;
            SceneData = prepared.Scene;
            ViewDirty = SceneDirty = true;
            const auto lighting = ResolveLighting(scene.lights, true);
            ConstantBuffers::UploadLighting(*Context.Get(), GetSceneBuffers(), lighting);
            UploadPendingConstants();
            if (lighting.ShadowActive) RenderShadowPass(prepared);
            ConstantBuffers::Upload(*Context.Get(), SkyboxViewBuffer.Get(), prepared.Skybox.Constants);
            const auto iblBindings = GetSceneBindings();
            const SC::IBLConstants iblConstants{
                iblBindings.IBLAvailable ? 1u : 0u, iblBindings.PrefilterMipCount };
            ConstantBuffers::Upload(*Context.Get(), IBLBuffer.Get(), iblConstants);
            auto draw = GetDrawContext();
            draw.BonePalettes = std::span<const Matrix4>{ prepared.BonePalettes };
            draw.Scene.EnvironmentView = prepared.Skybox.Texture;
            draw.Scene.SkyboxConstants = SkyboxViewBuffer.Get();
            if (scene.mirrorPlaneHeight) RenderMirrorPass(prepared, draw, *scene.mirrorPlaneHeight);
            BindSharedConstants();
            MainPass::DrawOpaqueAndMasked(prepared, draw);
            if (AfterOpaqueCallback) AfterOpaqueCallback();
            DrawSkybox(prepared.Skybox);
            MainPass::DrawTransparent(prepared, draw);
            if (AfterTransparentCallback) AfterTransparentCallback();
        }
    }

    void DX11Renderer::Impl::EndFrame()
    {
        if (!FrameActive)
        {
            return;
        }

        {
#if CANAPE_RENDER_DEBUG
            FinalOutputScope scope(ProfilerAvailable, Profiler, Context.Get());
#endif

            ResolveSceneColor();

            // UI goes after final output into a UNORM view of the same target, so the editor
            // viewport shows it too. The overlay (editor ImGui) still draws last.
            if (!Headless)
            {
                if (Overlay)
                {
                    ExecutePostProcessing(DisplayRenderTargetView.Get(), Width, Height);
                    ExecuteUIPass(DisplayUIRenderTargetView.Get(), Width, Height);
                    RenderOverlay();
                }
                else
                {
                    ExecutePostProcessing(RenderTargetView.Get(), BackBufferWidth, BackBufferHeight);
                    ExecuteUIPass(OverlayRenderTargetView.Get(), BackBufferWidth, BackBufferHeight);
                }
            }
        }
        PendingUI = nullptr;

        FrameActive = false;
        recordDebugStat();

        if (Headless) return;

        const HRESULT result = SwapChain->Present(1, 0);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("IDXGISwapChain::Present", result);
            CN_LOG_RENDERER("Error", LastError);
        }
    }
}
