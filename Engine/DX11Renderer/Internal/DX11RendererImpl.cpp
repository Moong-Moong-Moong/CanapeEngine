#include "DX11Renderer/Internal/DX11RendererImpl.h"
#include "CNDebug/DebugLog.h"
#include <array>
#include <utility>

namespace Canape
{
    namespace
    {
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
        TestNormalTexture = {};
        Pipelines = DX11PipelineState{};
        ShadowMap = {};
        SkyboxVertexBuffer.Reset();
        SkyboxIndexBuffer.Reset();
        FallbackShadowSampler.Reset();
        RenderTargetView.Reset();
        OverlayRenderTargetView.Reset();
        SceneColorTexture.Reset();
        SceneColorRenderTargetView.Reset();
        SceneColorShaderResourceView.Reset();
        DisplayTexture.Reset();
        DisplayRenderTargetView.Reset();
        DisplayShaderResourceView.Reset();
        RetiredDisplayShaderResourceView.Reset();
        DepthTexture.Reset();
        DepthStencilView.Reset();
#if CANAPE_RENDER_DEBUG
        DepthShaderResourceView.Reset();
        DepthDisplayBuffer.Reset();
#endif
        ObjectBuffer.Reset();
        MaterialBuffer.Reset();
        ViewBuffer.Reset();
        SkyboxViewBuffer.Reset();
        SceneBuffer.Reset();
        LightBuffer.Reset();
        PostProcessBuffer.Reset();
        SwapChain.Reset();
        Context.Reset();
        Device.Reset();

        Window = nullptr;
        Width = Height = BackBufferWidth = BackBufferHeight = 0;
        RequestedSceneWidth = RequestedSceneHeight = 0;
        Overlay = {};
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

    SceneBindings DX11Renderer::Impl::GetSceneBindings() const
    {
        ID3D11SamplerState* sampler = ShadowSettings.pcf ? ShadowMap.PcfSampler.Get() : ShadowMap.PointSampler.Get();
        if (!sampler) sampler = FallbackShadowSampler.Get();
        return { GetSceneBuffers(), ShadowActive ? ShadowMap.ShaderView.Get() : nullptr, sampler,
            nullptr, Pipelines.ResolveSkybox().Sampler, nullptr };
    }

    MeshDrawContext DX11Renderer::Impl::GetDrawContext()
    {
        MeshDrawContext result{ *Context.Get(), Pipelines, ObjectBuffer.Get(), MaterialBuffer.Get(), GetSceneBindings() };
        if (const auto* texture = Resources.GetTexture(TestNormalTexture))
            result.TestNormalTexture = texture->ShaderResourceView.Get();
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
        // Preserve linear HDR lighting and blending until final composition.
        D3D11_TEXTURE2D_DESC sceneColorDesc{};
        sceneColorDesc.Width = width;
        sceneColorDesc.Height = height;
        sceneColorDesc.MipLevels = 1;
        sceneColorDesc.ArraySize = 1;
        sceneColorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        sceneColorDesc.SampleDesc.Count = 1;
        sceneColorDesc.Usage = D3D11_USAGE_DEFAULT;
        sceneColorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT result = Device->CreateTexture2D(&sceneColorDesc, nullptr, &SceneColorTexture);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateTexture2D(scene color)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        D3D11_RENDER_TARGET_VIEW_DESC sceneTargetDesc{};
        sceneTargetDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        sceneTargetDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        result = Device->CreateRenderTargetView(SceneColorTexture.Get(), &sceneTargetDesc, &SceneColorRenderTargetView);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateRenderTargetView(scene color)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC sceneResourceDesc{};
        sceneResourceDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        sceneResourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sceneResourceDesc.Texture2D.MipLevels = 1;
        result = Device->CreateShaderResourceView(SceneColorTexture.Get(), &sceneResourceDesc, &SceneColorShaderResourceView);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateShaderResourceView(scene color)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        // Existing editor viewport output: SDR storage, sRGB encoding, raw UNORM UI sampling.
        // This is the final output destination when an overlay is installed, not an HDR intermediate.
        D3D11_TEXTURE2D_DESC displayDesc = sceneColorDesc;
        displayDesc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        sceneTargetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        result = Device->CreateTexture2D(&displayDesc, nullptr, &DisplayTexture);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateTexture2D(display)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }
        result = Device->CreateRenderTargetView(DisplayTexture.Get(), &sceneTargetDesc, &DisplayRenderTargetView);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateRenderTargetView(display)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }
        sceneResourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        result = Device->CreateShaderResourceView(DisplayTexture.Get(), &sceneResourceDesc, &DisplayShaderResourceView);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateShaderResourceView(display)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
#if CANAPE_RENDER_DEBUG
        depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
#else
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
#endif
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
#if CANAPE_RENDER_DEBUG
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
#else
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
#endif

        result = Device->CreateTexture2D(&depthDesc, nullptr, &DepthTexture);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateTexture2D(depth)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

        D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc{};
        depthViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        result = Device->CreateDepthStencilView(DepthTexture.Get(), &depthViewDesc, &DepthStencilView);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateDepthStencilView", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }

#if CANAPE_RENDER_DEBUG
        D3D11_SHADER_RESOURCE_VIEW_DESC depthResourceDesc{};
        depthResourceDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        depthResourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        depthResourceDesc.Texture2D.MipLevels = 1;
        result = Device->CreateShaderResourceView(DepthTexture.Get(), &depthResourceDesc, &DepthShaderResourceView);
        if (FAILED(result))
        {
            LastError = RendererHResultMessage("ID3D11Device::CreateShaderResourceView(depth)", result);
            CN_LOG_RENDERER("Error", LastError);
            return false;
        }
#endif

        Width = width;
        Height = height;
        return true;
    }

    bool DX11Renderer::Impl::ResizeIfNeeded(uint32_t width, uint32_t height)
    {
        const uint32_t sceneWidth = RequestedSceneWidth ? RequestedSceneWidth : width;
        const uint32_t sceneHeight = RequestedSceneHeight ? RequestedSceneHeight : height;
        const bool backBufferChanged = width != BackBufferWidth || height != BackBufferHeight;
        const bool sceneChanged = sceneWidth != Width || sceneHeight != Height;
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
            DisplayTexture.Reset();
            SceneColorShaderResourceView.Reset();
            SceneColorRenderTargetView.Reset();
            SceneColorTexture.Reset();
#if CANAPE_RENDER_DEBUG
            DepthShaderResourceView.Reset();
#endif
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
