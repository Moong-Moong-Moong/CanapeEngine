#include "DX11Renderer/Internal/DX11RendererImpl.h"
#include "CNDebug/DebugLog.h"
#include <array>
#include <utility>

namespace Canape
{
    namespace
    {
        constexpr DXGI_FORMAT SceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        constexpr DXGI_FORMAT CameraDepthViewFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        struct SkyboxVertex { float Position[2]; };
        static_assert(sizeof(SkyboxVertex) == sizeof(float) * 2);
    }

    DX11Renderer::Impl::Impl()
    {
        DirectX::XMStoreFloat4x4(&ViewData.View, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&ViewData.Projection, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&ViewData.ViewProjection, DirectX::XMMatrixIdentity());
    }

    void DX11Renderer::Impl::SetPostProcessSettings(const PostProcessSettings& settings)
    {
        postProcessSettings_ = settings;
    }

    DX11Renderer::Impl::~Impl()
    {
        Shutdown();
    }

    void DX11Renderer::Impl::InitializeSceneSampleDesc()
    {
        // Called once after device creation, before targets; resize does not recheck support.
        SelectedSceneSampleDesc = { 1, 0 };
        std::string fallbackReason;
        if constexpr (RequestedSceneSampleCount == 4)
        {
            UINT colorQualityLevels = 0;
            UINT depthQualityLevels = 0;
            UINT colorFormatSupport = 0;
            const HRESULT colorResult = Device->CheckMultisampleQualityLevels(
                SceneColorFormat, RequestedSceneSampleCount, &colorQualityLevels);
            // Check the typed DSV format, including when the debug depth texture is typeless.
            const HRESULT depthResult = Device->CheckMultisampleQualityLevels(
                CameraDepthViewFormat, RequestedSceneSampleCount, &depthQualityLevels);
            const HRESULT resolveResult = Device->CheckFormatSupport(SceneColorFormat, &colorFormatSupport);

            if (FAILED(colorResult))
                fallbackReason = RendererHResultMessage(
                    "CheckMultisampleQualityLevels(SceneColor R16G16B16A16_FLOAT, 4x)", colorResult);
            else if (colorQualityLevels == 0)
                fallbackReason = "SceneColor R16G16B16A16_FLOAT has no 4x quality levels";
            else if (FAILED(depthResult))
                fallbackReason = RendererHResultMessage(
                    "CheckMultisampleQualityLevels(camera depth D24_UNORM_S8_UINT, 4x)", depthResult);
            else if (depthQualityLevels == 0)
                fallbackReason = "Camera depth D24_UNORM_S8_UINT has no 4x quality levels";
            else if (FAILED(resolveResult))
                fallbackReason = RendererHResultMessage("CheckFormatSupport(SceneColor)", resolveResult);
            else if ((colorFormatSupport & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE) == 0)
                fallbackReason = "SceneColor R16G16B16A16_FLOAT does not support MULTISAMPLE_RESOLVE";
            else
                SelectedSceneSampleDesc = { RequestedSceneSampleCount, 0 };
        }

        CN_LOG_RENDERER("MSAA", "Requested scene samples=", RequestedSceneSampleCount,
            "; selected scene samples=", SelectedSceneSampleDesc.Count,
            "; quality=", SelectedSceneSampleDesc.Quality,
            "; fallback=", fallbackReason.empty() ? "no" : "yes",
            "; reason=", fallbackReason.empty() ? "none" : fallbackReason.c_str(),
            "; capability selection (scene targets not created yet)");
    }

#if CANAPE_RENDER_DEBUG
    void DX11Renderer::Impl::InitializeProfiler()
    {
        std::string profilerError;
        ProfilerAvailable = Profiler.Initialize(Device.Get(), profilerError);
        if (!ProfilerAvailable) CN_LOG_RENDERER("Profiler", profilerError);
    }
#endif

    void DX11Renderer::Impl::Shutdown()
    {
        // Release context-held references before releasing the owned GPU resources.
        if (Context) Context->ClearState();

        Initialized = false;
        FrameActive = false;
#if CANAPE_RENDER_DEBUG
        Profiler = GPUProfiler{};
        ProfilerAvailable = false;
        DebugSettings = {};
        CurrentStats = {};
        CompletedStats = {};
        NextFrameId = 0;
        MinGpuFrameId = 0;
#endif
        Resources.Clear();
        EnvironmentIBL = {};
        BRDFLUT = {};
        IBLBaker = DX11IBLBaker{};
        IBLBakerAttempted = IBLBakerReady = false;
        FailedIBLSourceId = {};
        FailedIBLSourceView = nullptr;
        Pipelines = DX11PipelineState{};
        ShadowMap = {};
        SkyboxVertexBuffer.Reset();
        SkyboxIndexBuffer.Reset();
        FallbackShadowSampler.Reset();
        RenderTargetView.Reset();
        OverlayRenderTargetView.Reset();
        SceneRenderTexture.Reset();
        SceneColorTexture.Reset();
        SceneColorRenderTargetView.Reset();
        SceneColorShaderResourceView.Reset();
        DisplayTexture.Reset();
        DisplayRenderTargetView.Reset();
        DisplayUIRenderTargetView.Reset();
        DisplayShaderResourceView.Reset();
        RetiredDisplayShaderResourceView.Reset();
        DepthTexture.Reset();
        DepthStencilView.Reset();
        DepthShaderResourceView.Reset();
#if CANAPE_RENDER_DEBUG
        DepthDisplayBuffer.Reset();
#endif
        ObjectBuffer.Reset();
        SkinningBuffer.Reset();
        MaterialBuffer.Reset();
        ViewBuffer.Reset();
        SkyboxViewBuffer.Reset();
        IBLBuffer.Reset();
        SceneBuffer.Reset();
        LightBuffer.Reset();
        PostProcessBuffer.Reset();
        UIBuffers = {};
        PendingUI = nullptr;
        SwapChain.Reset();
        Context.Reset();
        Device.Reset();
        SelectedSceneSampleDesc = { 1, 0 };

        Window = nullptr;
        Width = Height = BackBufferWidth = BackBufferHeight = 0;
        RequestedSceneWidth = RequestedSceneHeight = 0;
        Overlay = {};
        AfterOpaqueCallback = {};
        AfterTransparentCallback = {};
        LastError.clear();
        ViewData = {};
        DirectX::XMStoreFloat4x4(&ViewData.View, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&ViewData.Projection, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&ViewData.ViewProjection, DirectX::XMMatrixIdentity());
        SceneData = {};
        ViewDirty = SceneDirty = true;
        QueuedScene = {};
        ShadowSettings = {};
        postProcessSettings_ = {};
        ShadowActive = false;
        AttemptedShadowResolution = UINT32_MAX;
        IBLSettings = {};
    }

    bool DX11Renderer::Impl::EnsureShadowResources()
    {
        if (ShadowMap.Texture && ShadowMap.Resolution == ShadowSettings.resolution) return true;
        // Retry after a resolution change or renderer reinitialization, without logging every frame.
        if (AttemptedShadowResolution == ShadowSettings.resolution) return false;
        AttemptedShadowResolution = ShadowSettings.resolution;
        auto failed = [](const char* operation, HRESULT result)
        {
            CN_LOG_RENDERER("Shadow", RendererHResultMessage(operation, result), "; rendering without shadows");
            (void)operation;
            (void)result;
            return false;
        };
        if (ShadowSettings.resolution == 0 || ShadowSettings.resolution > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            return failed("Shadow map resolution", E_INVALIDARG);

        // Publish a complete set only. A failed recreation never exposes a partial texture/view pair.
        ShadowMapResources pending;
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = texture.Height = ShadowSettings.resolution;
        texture.MipLevels = texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_R32_TYPELESS;
        texture.SampleDesc.Count = 1;
        texture.Usage = D3D11_USAGE_DEFAULT;
        texture.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        HRESULT result = Device->CreateTexture2D(&texture, nullptr, &pending.Texture);
        if (FAILED(result)) return failed("CreateTexture2D(shadow)", result);
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        result = Device->CreateDepthStencilView(pending.Texture.Get(), &dsv, &pending.DepthView);
        if (FAILED(result)) return failed("CreateDepthStencilView(shadow)", result);
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        result = Device->CreateShaderResourceView(pending.Texture.Get(), &srv, &pending.ShaderView);
        if (FAILED(result)) return failed("CreateShaderResourceView(shadow)", result);
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        sampler.BorderColor[0] = sampler.BorderColor[1] = sampler.BorderColor[2] = sampler.BorderColor[3] = 1.0f;
        sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        sampler.MaxAnisotropy = 1;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        result = Device->CreateSamplerState(&sampler, &pending.PointSampler);
        if (FAILED(result)) return failed("CreateSamplerState(shadow point comparison)", result);
        sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        result = Device->CreateSamplerState(&sampler, &pending.PcfSampler);
        if (FAILED(result)) return failed("CreateSamplerState(shadow PCF comparison)", result);
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(SC::ShadowConstants);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        result = Device->CreateBuffer(&buffer, nullptr, &pending.Constants);
        if (FAILED(result)) return failed("CreateBuffer(shadow constants)", result);
        buffer.ByteWidth = sizeof(SC::PcssConstants);
        result = Device->CreateBuffer(&buffer, nullptr, &pending.PcssConstants);
        if (FAILED(result)) return failed("CreateBuffer(PCSS constants)", result);
        pending.Resolution = ShadowSettings.resolution;
        ID3D11ShaderResourceView* unbound = nullptr;
        Context->PSSetShaderResources(1, 1, &unbound);
        ShadowMap = std::move(pending);
        return true;
    }

    SceneConstantBuffers DX11Renderer::Impl::GetSceneBuffers() const
    {
        return { ViewBuffer.Get(), SceneBuffer.Get(), LightBuffer.Get(),
            ShadowMap.Constants.Get(), ShadowMap.PcssConstants.Get() };
    }

    void DX11Renderer::Impl::BindMainTargets()
    {
        RenderBindings::BindMainTargets(*Context.Get(), { SceneColorRenderTargetView.Get(),
            DepthStencilView.Get(), Width, Height });
    }

    MainTargets DX11Renderer::Impl::GetMainTargets() const
    {
        return { SceneColorRenderTargetView.Get(), DepthStencilView.Get(), Width, Height };
    }

    void DX11Renderer::Impl::ResolveSceneColor()
    {
        if (SelectedSceneSampleDesc.Count == 1) return;

        // Scene color/depth reads use PS t0. Release reads and all scene outputs before resolve.
        Context->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* unbound = nullptr;
        Context->PSSetShaderResources(0, 1, &unbound);
        Context->ResolveSubresource(SceneColorTexture.Get(), 0, SceneRenderTexture.Get(), 0, SceneColorFormat);
    }

    SceneBindings DX11Renderer::Impl::GetSceneBindings() const
    {
        ID3D11SamplerState* sampler = ShadowSettings.pcf ? ShadowMap.PcfSampler.Get() : ShadowMap.PointSampler.Get();
        if (!sampler) sampler = FallbackShadowSampler.Get();
        SceneBindings bindings{ GetSceneBuffers(), ShadowActive ? ShadowMap.ShaderView.Get() : nullptr,
            sampler, nullptr, Pipelines.ResolveSkybox().Sampler, nullptr };
        bindings.IBLBuffer = IBLBuffer.Get();
        bindings.BRDFSampler = Pipelines.ResolveSkybox().Sampler;
        if (EnvironmentIBL.SourceId.IsValid() && EnvironmentIBL.Irradiance.ShaderResourceView
            && EnvironmentIBL.Prefiltered.ShaderResourceView && BRDFLUT.ShaderResourceView)
        {
            bindings.IrradianceView = EnvironmentIBL.Irradiance.ShaderResourceView.Get();
            bindings.PrefilteredView = EnvironmentIBL.Prefiltered.ShaderResourceView.Get();
            bindings.BRDFView = BRDFLUT.ShaderResourceView.Get();
            bindings.PrefilterMipCount = EnvironmentIBL.PrefilterMipCount;
            bindings.IBLAvailable = true;
        }
        return bindings;
    }

    MeshDrawContext DX11Renderer::Impl::GetDrawContext()
    {
        MeshDrawContext result{ *Context.Get(), Pipelines, ObjectBuffer.Get(), MaterialBuffer.Get(), GetSceneBindings() };
        result.SkinningBuffer = SkinningBuffer.Get();
#if CANAPE_RENDER_DEBUG
        result.DebugSettings = &DebugSettings;
        result.Stats = &CurrentStats;
#endif
        return result;
    }

    void DX11Renderer::Impl::BindSharedConstants()
    {
        ID3D11SamplerState* sampler = ShadowSettings.pcf
            ? ShadowMap.PcfSampler.Get() : ShadowMap.PointSampler.Get();
        if (!sampler) sampler = FallbackShadowSampler.Get();
        RenderBindings::BindScene(*Context.Get(), { GetSceneBuffers(),
            ShadowActive ? ShadowMap.ShaderView.Get() : nullptr, sampler });
    }

    void DX11Renderer::Impl::BindPipeline(const DX11PipelineSelection& pipeline)
    {
        RenderBindings::BindPipeline(*Context.Get(), pipeline);
    }

    void DX11Renderer::Impl::ExecuteUIPass(ID3D11RenderTargetView* target, uint32_t width, uint32_t height)
    {
        const UIDrawList* list = PendingUI;
        PendingUI = nullptr;
        if (!list || list->IsEmpty() || !Pipelines.UIAvailable()) return;
        RendererDebugStats* stats = nullptr;
#if CANAPE_RENDER_DEBUG
        stats = &CurrentStats;
#endif
        // World-space UI compares against the camera depth; only valid when the target matches the scene size.
        ID3D11ShaderResourceView* depth = (width == Width && height == Height) ? DepthShaderResourceView.Get() : nullptr;
        const bool multisampledDepth = SelectedSceneSampleDesc.Count > 1;
        std::string error;
        if (!UIPass::Execute(*list, Pipelines.ResolveUI(multisampledDepth), Resources, UIBuffers,
                *Device.Get(), *Context.Get(), target, depth, width, height, stats, error))
        {
            LastError = error;
            CN_LOG_RENDERER("UI", LastError);
        }
    }

    void DX11Renderer::Impl::RenderOverlay()
    {
        ID3D11RenderTargetView* target = OverlayRenderTargetView.Get();
        Context->OMSetRenderTargets(1, &target, nullptr);

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(BackBufferWidth);
        viewport.Height = static_cast<float>(BackBufferHeight);
        viewport.MaxDepth = 1.0f;
        Context->RSSetViewports(1, &viewport);

        const float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
        Context->ClearRenderTargetView(target, clearColor);
        Overlay();
    }

    bool DX11Renderer::Impl::CreateTargets(uint32_t width, uint32_t height)
    {
        return CreateBackBufferTargets(width, height) && CreateSceneTargets(width, height);
    }

    bool DX11Renderer::Impl::CreateBackBufferTargets(uint32_t width, uint32_t height)
    {
        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT result = SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("IDXGISwapChain::GetBuffer", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        // Keep the UNORM swap-chain storage; encode linear RGB once through its sRGB view.
        // CreateTargets is also used after ResizeBuffers, so resize preserves this policy.
        D3D11_RENDER_TARGET_VIEW_DESC renderTargetDesc{};
        renderTargetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        renderTargetDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        result = Device->CreateRenderTargetView(backBuffer.Get(), &renderTargetDesc, &RenderTargetView);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateRenderTargetView(back buffer, RGBA8_UNORM_SRGB)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        renderTargetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        result = Device->CreateRenderTargetView(backBuffer.Get(), &renderTargetDesc, &OverlayRenderTargetView);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateRenderTargetView(back buffer, RGBA8_UNORM)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        BackBufferWidth = width;
        BackBufferHeight = height;
        return true;
    }

    bool DX11Renderer::Impl::CreateSceneTargets(uint32_t width, uint32_t height)
    {
        std::string error;
        const auto createTargets = [this, width, height, &error]()
        {
            auto failed = [&error](const char* operation, HRESULT result)
            {
                error = RendererHResultMessage(operation, result);
                return false;
            };
            // Publish a complete set only. A failed attempt releases all partial resources
            // before retrying, including the shared ComPtr references used in 1x mode.
            ComPtr<ID3D11Texture2D> sceneRenderTexture;
            ComPtr<ID3D11Texture2D> sceneColorTexture;
            ComPtr<ID3D11RenderTargetView> sceneRenderTargetView;
            ComPtr<ID3D11ShaderResourceView> sceneColorShaderResourceView;
            ComPtr<ID3D11Texture2D> displayTexture;
            ComPtr<ID3D11RenderTargetView> displayRenderTargetView;
            ComPtr<ID3D11RenderTargetView> displayUIRenderTargetView;
            ComPtr<ID3D11ShaderResourceView> displayShaderResourceView;
            ComPtr<ID3D11Texture2D> depthTexture;
            ComPtr<ID3D11DepthStencilView> depthStencilView;
            ComPtr<ID3D11ShaderResourceView> depthShaderResourceView;
            const bool multisampled = SelectedSceneSampleDesc.Count > 1;
            D3D11_TEXTURE2D_DESC sceneRenderDesc{};
            sceneRenderDesc.Width = width;
            sceneRenderDesc.Height = height;
            sceneRenderDesc.MipLevels = 1;
            sceneRenderDesc.ArraySize = 1;
            sceneRenderDesc.Format = SceneColorFormat;
            sceneRenderDesc.SampleDesc = SelectedSceneSampleDesc;
            sceneRenderDesc.Usage = D3D11_USAGE_DEFAULT;
            sceneRenderDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (!multisampled) sceneRenderDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            HRESULT result = Device->CreateTexture2D(&sceneRenderDesc, nullptr, &sceneRenderTexture);
            if (FAILED(result)) return failed("ID3D11Device::CreateTexture2D(scene render)", result);

            // Post processing always reads a single-sample HDR texture.
            D3D11_TEXTURE2D_DESC sceneColorDesc = sceneRenderDesc;
            sceneColorDesc.SampleDesc = { 1, 0 };
            if (multisampled)
            {
                sceneColorDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                result = Device->CreateTexture2D(&sceneColorDesc, nullptr, &sceneColorTexture);
                if (FAILED(result)) return failed("ID3D11Device::CreateTexture2D(scene color resolve)", result);
            }
            else
            {
                sceneColorTexture = sceneRenderTexture;
            }

            D3D11_RENDER_TARGET_VIEW_DESC sceneTargetDesc{};
            sceneTargetDesc.Format = SceneColorFormat;
            sceneTargetDesc.ViewDimension = multisampled
                ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
            result = Device->CreateRenderTargetView(sceneRenderTexture.Get(), &sceneTargetDesc, &sceneRenderTargetView);
            if (FAILED(result)) return failed("ID3D11Device::CreateRenderTargetView(scene render)", result);

            D3D11_SHADER_RESOURCE_VIEW_DESC sceneResourceDesc{};
            sceneResourceDesc.Format = SceneColorFormat;
            sceneResourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sceneResourceDesc.Texture2D.MipLevels = 1;
            result = Device->CreateShaderResourceView(sceneColorTexture.Get(), &sceneResourceDesc, &sceneColorShaderResourceView);
            if (FAILED(result)) return failed("ID3D11Device::CreateShaderResourceView(scene color)", result);

            // SDR editor viewport output and UI remain single-sampled in both modes.
            D3D11_TEXTURE2D_DESC displayDesc = sceneColorDesc;
            displayDesc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            displayDesc.SampleDesc = { 1, 0 };
            displayDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            result = Device->CreateTexture2D(&displayDesc, nullptr, &displayTexture);
            if (FAILED(result)) return failed("ID3D11Device::CreateTexture2D(display)", result);

            D3D11_RENDER_TARGET_VIEW_DESC displayTargetDesc{};
            displayTargetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            displayTargetDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            result = Device->CreateRenderTargetView(displayTexture.Get(), &displayTargetDesc, &displayRenderTargetView);
            if (FAILED(result)) return failed("ID3D11Device::CreateRenderTargetView(display)", result);
            displayTargetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            result = Device->CreateRenderTargetView(displayTexture.Get(), &displayTargetDesc, &displayUIRenderTargetView);
            if (FAILED(result)) return failed("ID3D11Device::CreateRenderTargetView(display UI)", result);
            sceneResourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            result = Device->CreateShaderResourceView(displayTexture.Get(), &sceneResourceDesc, &displayShaderResourceView);
            if (FAILED(result)) return failed("ID3D11Device::CreateShaderResourceView(display)", result);

            D3D11_TEXTURE2D_DESC depthDesc{};
            depthDesc.Width = width;
            depthDesc.Height = height;
            depthDesc.MipLevels = 1;
            depthDesc.ArraySize = 1;
            // Typeless + shader-readable in every configuration: world-space UI compares against it.
            depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
            depthDesc.SampleDesc = SelectedSceneSampleDesc;
            depthDesc.Usage = D3D11_USAGE_DEFAULT;
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            result = Device->CreateTexture2D(&depthDesc, nullptr, &depthTexture);
            if (FAILED(result)) return failed("ID3D11Device::CreateTexture2D(depth)", result);

            D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc{};
            depthViewDesc.Format = CameraDepthViewFormat;
            depthViewDesc.ViewDimension = multisampled
                ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
            result = Device->CreateDepthStencilView(depthTexture.Get(), &depthViewDesc, &depthStencilView);
            if (FAILED(result)) return failed("ID3D11Device::CreateDepthStencilView", result);
            D3D11_SHADER_RESOURCE_VIEW_DESC depthResourceDesc{};
            depthResourceDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            depthResourceDesc.ViewDimension = multisampled
                ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
            if (!multisampled) depthResourceDesc.Texture2D.MipLevels = 1;
            result = Device->CreateShaderResourceView(depthTexture.Get(), &depthResourceDesc, &depthShaderResourceView);
            if (FAILED(result)) return failed("ID3D11Device::CreateShaderResourceView(depth)", result);

            SceneRenderTexture = std::move(sceneRenderTexture);
            SceneColorTexture = std::move(sceneColorTexture);
            SceneColorRenderTargetView = std::move(sceneRenderTargetView);
            SceneColorShaderResourceView = std::move(sceneColorShaderResourceView);
            DisplayTexture = std::move(displayTexture);
            DisplayRenderTargetView = std::move(displayRenderTargetView);
            DisplayUIRenderTargetView = std::move(displayUIRenderTargetView);
            DisplayShaderResourceView = std::move(displayShaderResourceView);
            DepthTexture = std::move(depthTexture);
            DepthStencilView = std::move(depthStencilView);
            DepthShaderResourceView = std::move(depthShaderResourceView);
            Width = width;
            Height = height;
            return true;
        };

        bool created = createTargets();
        if (!created && SelectedSceneSampleDesc.Count > 1)
        {
            CN_LOG_RENDERER("MSAA", error, "; falling back to 1x for scene color and camera depth");
            SelectedSceneSampleDesc = { 1, 0 };
            created = createTargets();
        }
        if (!created)
        {
            LastError = std::move(error);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }
        CN_LOG_RENDERER("MSAA", "Created scene targets: requested=", RequestedSceneSampleCount,
            "; actual samples=", SelectedSceneSampleDesc.Count, "; quality=", SelectedSceneSampleDesc.Quality,
            "; post processing/display/back buffer=1x");
        return true;
    }

    bool DX11Renderer::Impl::ResizeIfNeeded(uint32_t width, uint32_t height)
    {
        const uint32_t sceneWidth = RequestedSceneWidth ? RequestedSceneWidth : width;
        const uint32_t sceneHeight = RequestedSceneHeight ? RequestedSceneHeight : height;
        const bool backBufferChanged = width != BackBufferWidth || height != BackBufferHeight;
        const bool sceneChanged = sceneWidth != Width || sceneHeight != Height
            || !SceneRenderTexture || !SceneColorTexture || !DepthTexture;
        if (!backBufferChanged && !sceneChanged)
        {
            return true;
        }

        Context->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* unbound = nullptr;
        Context->PSSetShaderResources(0, 1, &unbound);

        if (sceneChanged)
        {
            RetiredDisplayShaderResourceView = std::move(DisplayShaderResourceView);
            DisplayRenderTargetView.Reset();
            DisplayUIRenderTargetView.Reset();
            DisplayTexture.Reset();
            SceneColorShaderResourceView.Reset();
            SceneColorRenderTargetView.Reset();
            SceneRenderTexture.Reset();
            SceneColorTexture.Reset();
            DepthShaderResourceView.Reset();
            DepthStencilView.Reset();
            DepthTexture.Reset();
        }

        if (backBufferChanged)
        {
            RenderTargetView.Reset();
            OverlayRenderTargetView.Reset();
            const HRESULT result = SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(result))
            {
                LastError = RendererHResultMessage("IDXGISwapChain::ResizeBuffers", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }
            if (!CreateBackBufferTargets(width, height))
            {
                return false;
            }
        }

        return !sceneChanged || CreateSceneTargets(sceneWidth, sceneHeight);
    }

    bool DX11Renderer::Impl::CreateSkyboxQuad()
    {
        constexpr std::array<SkyboxVertex, 4> vertices{{
            {{ -1.0f,  1.0f }},
            {{  1.0f,  1.0f }},
            {{  1.0f, -1.0f }},
            {{ -1.0f, -1.0f }}
        }};
        constexpr std::array<uint32_t, SkyboxIndexCount> indices{ 0, 1, 2, 0, 2, 3 };

        D3D11_BUFFER_DESC vertexDesc{};
        vertexDesc.ByteWidth = sizeof(vertices);
        vertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertexData{ vertices.data() };
        ComPtr<ID3D11Buffer> vertexBuffer;
        HRESULT result = Device->CreateBuffer(&vertexDesc, &vertexData, &vertexBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(skybox vertices)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        D3D11_BUFFER_DESC indexDesc{};
        indexDesc.ByteWidth = sizeof(indices);
        indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
        indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA indexData{ indices.data() };
        ComPtr<ID3D11Buffer> indexBuffer;
        result = Device->CreateBuffer(&indexDesc, &indexData, &indexBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(skybox indices)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        SkyboxVertexBuffer = std::move(vertexBuffer);
        SkyboxIndexBuffer = std::move(indexBuffer);
        return true;
    }

    bool DX11Renderer::Impl::CreatePipeline()
    {
        if (!Pipelines.Initialize(Device.Get(), LastError)) return false;

        D3D11_BUFFER_DESC constantBufferDesc{};
        constantBufferDesc.ByteWidth = sizeof(SC::ObjectConstants);
        constantBufferDesc.Usage = D3D11_USAGE_DEFAULT;
        constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        HRESULT result = Device->CreateBuffer(&constantBufferDesc, nullptr, &ObjectBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(constants)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        constantBufferDesc.ByteWidth = sizeof(SC::SkinningConstants);
        result = Device->CreateBuffer(&constantBufferDesc, nullptr, &SkinningBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(skinning constants)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        static_assert(sizeof(Vector4) == 16);
        constantBufferDesc.ByteWidth = sizeof(SC::MaterialConstants);
        result = Device->CreateBuffer(&constantBufferDesc, nullptr, &MaterialBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(material constants)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        constantBufferDesc.ByteWidth = sizeof(SC::ViewConstants);
        result = Device->CreateBuffer(&constantBufferDesc, nullptr, &ViewBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(view constants)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        constantBufferDesc.ByteWidth = sizeof(SC::SkyboxViewConstants);
        result = Device->CreateBuffer(&constantBufferDesc, nullptr, &SkyboxViewBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(skybox view constants)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        constantBufferDesc.ByteWidth = sizeof(SC::IBLConstants);
        result = Device->CreateBuffer(&constantBufferDesc, nullptr, &IBLBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(IBL constants)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        constantBufferDesc.ByteWidth = sizeof(SC::LightConstants);
        result = Device->CreateBuffer(&constantBufferDesc, nullptr, &LightBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(light constants)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        constantBufferDesc.ByteWidth = sizeof(SC::SceneConstants);
        result = Device->CreateBuffer(&constantBufferDesc, nullptr, &SceneBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(scene constants)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        constantBufferDesc.ByteWidth = sizeof(SC::PostProcessConstants);
        result = Device->CreateBuffer(&constantBufferDesc, nullptr, &PostProcessBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(post process constants)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

#if CANAPE_RENDER_DEBUG
        constantBufferDesc.ByteWidth = sizeof(SC::DepthDisplayConstants);
        result = Device->CreateBuffer(&constantBufferDesc, nullptr, &DepthDisplayBuffer);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateBuffer(depth display constants)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }
#endif

        // The PS declares a comparison sampler even when shadow branches are disabled.
        // Keep that slot valid before the first map allocation and on resource fallback.
        D3D11_SAMPLER_DESC shadowSampler{};
        shadowSampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        shadowSampler.AddressU = shadowSampler.AddressV = shadowSampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        shadowSampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        shadowSampler.MaxAnisotropy = 1;
        shadowSampler.MaxLOD = D3D11_FLOAT32_MAX;
        result = Device->CreateSamplerState(&shadowSampler, &FallbackShadowSampler);
        if (FAILED(result)) CN_LOG_RENDERER("Shadow", RendererHResultMessage("CreateSamplerState(shadow fallback)", result));

        return Resources.CreateWhiteTexture(Device.Get(), Context.Get(), LastError);
    }
}
