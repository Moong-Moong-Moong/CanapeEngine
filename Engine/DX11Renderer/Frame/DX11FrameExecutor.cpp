#include "DX11Renderer/Internal/DX11RendererImpl.h"
#include "DX11Renderer/Pass/DX11ShadowPass.h"
#include "DX11Renderer/Pass/DX11MainPass.h"
#include "DX11Renderer/Pass/DX11SkyboxPass.h"
#include "DX11Renderer/Pass/DX11FinalOutputPass.h"
#include "CNDebug/DebugLog.h"

namespace Canape
{
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
        ShadowPass::Execute(scene, draw, { ShadowMap.DepthView.Get(), ShadowMap.Resolution }, GetMainTargets());
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
        const auto output = ScenePreparation::PrepareFinalOutput(Pipelines, SceneColorShaderResourceView.Get(),
            depth, ShadowMap.ShaderView.Get(), ShadowActive, debug);
#if CANAPE_RENDER_DEBUG
        if (output.DisplayDepth) ConstantBuffers::Upload(*Context.Get(), depthDisplay, output.DepthDisplay);
#endif
        // Current single settings source. Future per-view integration should select
        // scene.camera.postProcess in ExecuteScene instead of these development defaults.
        SC::PostProcessConstants postProcess{};
        postProcess.PostProcessingEnabled = postProcessSettings_.enabled ? 1u : 0u;
        postProcess.ToneMappingEnabled = postProcessSettings_.toneMappingEnabled ? 1u : 0u;
#if CANAPE_RENDER_DEBUG
        postProcess.DebugViewEnabled = DebugSettings.enabled && DebugSettings.viewMode != DebugViewMode::Final ? 1u : 0u;
#endif
        ConstantBuffers::Upload(*Context.Get(), PostProcessBuffer.Get(), postProcess);
        FinalOutputPass::Execute(output, *Context.Get(), target, width, height,
            depthDisplay, PostProcessBuffer.Get(), stats);
    }

    void DX11Renderer::Impl::BeginFrameProfiling(ID3D11DeviceContext* context)
    {
#if CANAPE_RENDER_DEBUG

        CurrentStats = {};
        CurrentStats.frameId = ++NextFrameId;
        if (ProfilerAvailable)
        {
            Profiler.Poll(context);
            if (DebugSettings.captureGpuTimings)
                Profiler.BeginFrame(context, CurrentStats.frameId);
        }
#endif
    }

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

        BeginFrameProfiling(context);

        // Linear RGB and alpha are stored directly in the FP16 SceneColor target.
        const float clearColor[] = { 0.025f, 0.035f, 0.055f, 1.0f };
        context->ClearRenderTargetView(SceneColorRenderTargetView.Get(), clearColor);
        context->ClearDepthStencilView(DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        BindMainTargets();

        // Direct draws start with scene defaults instead of inheriting the previous frame.
        // Always upload a freshly initialized LightConstants, even with no scene draws.
        const auto lighting = ResolveLighting(QueuedScene.lights, false);
        ConstantBuffers::UploadLighting(*context, GetSceneBuffers(), lighting);
        SceneData = {};
        SceneDirty = true;
        // Reset the complete default pipeline, including frames ending in transparency.
        BindPipeline(Pipelines.Resolve(VertexFormat::Position, AlphaMode::Opaque));

        FrameActive = true;
        return true;
    }

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

    void DX11Renderer::Impl::ExecuteScene(const RenderScene& scene)
    {
        if (!FrameActive)
        {
            return;
        }

        {
            SceneScope scope(ProfilerAvailable, Profiler, Context.Get());

            const auto prepared = ScenePreparation::PrepareScene(scene, Resources, LastError);
            ViewData = prepared.View;
            SceneData = prepared.Scene;
            ViewDirty = SceneDirty = true;
            const auto lighting = ResolveLighting(scene.lights, true);
            ConstantBuffers::UploadLighting(*Context.Get(), GetSceneBuffers(), lighting);
            UploadPendingConstants();
            if (lighting.ShadowActive) RenderShadowPass(prepared);
            BindSharedConstants();
            auto draw = GetDrawContext();
            MainPass::DrawOpaqueAndMasked(prepared, draw);
            DrawSkybox(prepared.Skybox);
            MainPass::DrawTransparent(prepared, draw);
        }
    }

    void DX11Renderer::Impl::DrawMesh(MeshResourceId mesh, const float* world, uint32_t submeshIndex, MaterialResourceId material)
    {
        if (!FrameActive)
        {
            return;
        }

        PreparedMeshDraw prepared;
        if (!ScenePreparation::PrepareMeshDraw(Resources, mesh, world, submeshIndex,
            material, prepared, LastError)) return;
        UploadPendingConstants();
        auto draw = GetDrawContext();
        MeshDraw::SubmitMain(prepared, draw);
    }

    void DX11Renderer::Impl::EndFrame()
    {
        if (!FrameActive)
        {
            return;
        }

        {
            FinalOutputScope scope(ProfilerAvailable, Profiler, Context.Get());

            if (Overlay)
            {
                ExecutePostProcessing(DisplayRenderTargetView.Get(), Width, Height);
                RenderOverlay();
            }
            else
            {
                ExecutePostProcessing(RenderTargetView.Get(), BackBufferWidth, BackBufferHeight);
            }
        }

        FrameActive = false;
        recordDebugStat();

        const HRESULT result = SwapChain->Present(1, 0);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("IDXGISwapChain::Present", result);
            CN_LOG_RENDERER("Error", LastError);
        }
    }
}
