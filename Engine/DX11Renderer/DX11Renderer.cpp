#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "DX11Renderer/Internal/DX11RendererImpl.h"
#include "CNDebug/DebugLog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace Canape
{
    namespace SC = ShaderConstants;

    DX11Renderer::DX11Renderer()
        : m_Impl(std::make_unique<Impl>())
    {

    }

    DX11Renderer::~DX11Renderer() = default;

    bool DX11Renderer::Initialize(void* nativeWindow, uint32_t width, uint32_t height)
    {
        Shutdown();
        const auto fail = [this]()
        {
            std::string error = std::move(m_Impl->LastError);
            Shutdown();
            m_Impl->LastError = std::move(error);
            return false;
        };
        m_Impl->Window = static_cast<HWND>(nativeWindow);

        if (!m_Impl->Window || !IsWindow(m_Impl->Window))
        {
            m_Impl->LastError = "Initialize received an invalid native window";
            CN_LOG_RENDERER("Error", m_Impl->LastError);
            return fail();
        }

        DXGI_SWAP_CHAIN_DESC swapChainDesc{};
        swapChainDesc.BufferDesc.Width = width;
        swapChainDesc.BufferDesc.Height = height;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.OutputWindow = m_Impl->Window;
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL selectedFeatureLevel{};

        HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
            &swapChainDesc, &m_Impl->SwapChain, &m_Impl->Device,
            &selectedFeatureLevel, &m_Impl->Context);

        if (FAILED(result))
        {
            m_Impl->Context.Reset();
            m_Impl->SwapChain.Reset();
            m_Impl->Device.Reset();
            result = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
                &swapChainDesc, &m_Impl->SwapChain, &m_Impl->Device,
                &selectedFeatureLevel, &m_Impl->Context);
        }

        if (FAILED(result))
        {
            m_Impl->LastError = RendererHResultMessage("D3D11CreateDeviceAndSwapChain", result);
            CN_LOG_RENDERER("Error", m_Impl->LastError);
            return fail();
        }

        if (!m_Impl->CreateTargets(width, height) || !m_Impl->CreatePipeline()
            || !m_Impl->CreateSkyboxQuad()) return fail();
#if CANAPE_RENDER_DEBUG
        m_Impl->InitializeProfiler();
#endif
        m_Impl->Initialized = true;
        CN_LOG_RENDERER("Init", "렌더러 초기화 완료");
        return true;
    }

    void DX11Renderer::SetPostProcessSettings(const PostProcessSettings& settings)
    {
        m_Impl->SetPostProcessSettings(settings);
    }

    PostProcessSettings DX11Renderer::GetPostProcessSettings() const
    {
        return m_Impl->postProcessSettings_;
    }

    void DX11Renderer::Shutdown()
    {
        m_Impl->Shutdown();
    }

    MeshResourceId DX11Renderer::CreateMesh(
        std::span<const Vertex> vertices,
        std::span<const uint32_t> indices)
    {
        return CreateMeshData(vertices.data(), vertices.size(), sizeof(Vertex), VertexFormat::Position, indices, {});
    }

    MeshResourceId DX11Renderer::CreateMesh(std::span<const StaticVertex> vertices,
        std::span<const uint32_t> indices, std::span<const SubMesh> submeshes)
    {
        return CreateMeshData(vertices.data(), vertices.size(), sizeof(StaticVertex), VertexFormat::Static, indices, submeshes);
    }

    MeshResourceId DX11Renderer::CreateMeshData(const void* vertices, size_t vertexCount,
        uint32_t stride, VertexFormat format, std::span<const uint32_t> indices, std::span<const SubMesh> submeshes)
    {
        return m_Impl->Resources.CreateMesh(m_Impl->Device.Get(), vertices, vertexCount, stride, format,
            indices, submeshes, m_Impl->LastError);
    }

    void DX11Renderer::DestroyMesh(MeshResourceId mesh)
    {
        m_Impl->Resources.DestroyMesh(mesh);
    }

    bool DX11Renderer::IsMeshValid(MeshResourceId mesh) const
    {
        return m_Impl->Resources.IsMeshValid(mesh);
    }

    void DX11Renderer::SetCamera(const float* view, const float* projection)
    {
        m_Impl->ViewData = ScenePreparation::BuildViewConstants(view, projection);
        m_Impl->ViewDirty = true;
    }

    void DX11Renderer::SetDebugSettings(const RendererDebugSettings& settings)
    {
#if CANAPE_RENDER_DEBUG
        RendererDebugSettings valid = settings;
        if (valid.viewMode != DebugViewMode::Final && valid.viewMode != DebugViewMode::CameraDepth
            && valid.viewMode != DebugViewMode::WorldNormal && valid.viewMode != DebugViewMode::UV
            && valid.viewMode != DebugViewMode::MipLOD && valid.viewMode != DebugViewMode::ShadowDepth
            && valid.viewMode != DebugViewMode::ShadowVisibility
            && valid.viewMode != DebugViewMode::PcssFilterRadius)
            valid.viewMode = DebugViewMode::Final;
        const RendererDebugSettings defaults;
        if (!std::isfinite(valid.depthMin) || !std::isfinite(valid.depthMax)
            || valid.depthMin < 0.0f || valid.depthMax > 1.0f || valid.depthMin >= valid.depthMax)
        {
            valid.depthMin = defaults.depthMin;
            valid.depthMax = defaults.depthMax;
        }
        if (valid.shadowCullMode != ShadowCullMode::None && valid.shadowCullMode != ShadowCullMode::Front
            && valid.shadowCullMode != ShadowCullMode::Back)
            valid.shadowCullMode = defaults.shadowCullMode;
        if (!std::isfinite(valid.shadowSlopeBias) || valid.shadowSlopeBias < 0.0f)
            valid.shadowSlopeBias = defaults.shadowSlopeBias;
        if (!std::isfinite(valid.shadowReceiverBias) || valid.shadowReceiverBias < 0.0f)
            valid.shadowReceiverBias = defaults.shadowReceiverBias;
        if (m_Impl->Device && m_Impl->Pipelines.ShadowsAvailable()
            && (valid.shadowCullMode != m_Impl->DebugSettings.shadowCullMode
                || valid.shadowSlopeBias != m_Impl->DebugSettings.shadowSlopeBias))
        {
            std::string error;
            if (!m_Impl->Pipelines.SetShadowRasterizer(m_Impl->Device.Get(),
                valid.shadowCullMode, valid.shadowSlopeBias, error))
            {
                CN_LOG_RENDERER("Shadow", error);
                valid.shadowCullMode = m_Impl->DebugSettings.shadowCullMode;
                valid.shadowSlopeBias = m_Impl->DebugSettings.shadowSlopeBias;
            }
        }
        m_Impl->ShadowSettings.depthBias = valid.shadowReceiverBias;
        m_Impl->ShadowSettings.pcf = valid.shadowPcf;
        if (valid.captureGpuTimings && !m_Impl->DebugSettings.captureGpuTimings)
            m_Impl->MinGpuFrameId = m_Impl->NextFrameId + 1;
        m_Impl->DebugSettings = valid;
#else
        (void)settings;
#endif
    }

    RendererDebugSettings DX11Renderer::GetDebugSettings() const
    {
#if CANAPE_RENDER_DEBUG
        return m_Impl->DebugSettings;
#endif
        return {};
    }

    RendererDebugStats DX11Renderer::GetDebugStats() const
    {
        RendererDebugStats stats{};
#if CANAPE_RENDER_DEBUG
        stats = m_Impl->CompletedStats;
        if (m_Impl->DebugSettings.captureGpuTimings && m_Impl->ProfilerAvailable)
        {
            const GpuTimingResult& timing = m_Impl->Profiler.LatestResult();
            if (timing.frameId >= m_Impl->MinGpuFrameId)
            {
                stats.gpuFrameId = timing.frameId;
                stats.gpuTimingValid = timing.valid;
                stats.gpuFrameMs = timing.frameMs;
                stats.drawSceneMs = timing.drawSceneMs;
                stats.finalOutputMs = timing.finalOutputMs;
            }
        }
#endif
        return stats;
    }

    bool DX11Renderer::SetShadowFilterSettings(const ShadowFilterSettings& settings)
    {
        const auto isSampleCount = [](uint32_t count)
        {
            return std::find(std::begin(ShadowSampleCountOptions), std::end(ShadowSampleCountOptions), count)
                != std::end(ShadowSampleCountOptions);
        };
        if (!isSampleCount(settings.pcfSampleCount) || !isSampleCount(settings.blockerSampleCount)
            || !std::isfinite(settings.shadowDistance) || settings.shadowDistance < MinShadowDistance
            || !std::isfinite(settings.pcfTapSpacingScale) || settings.pcfTapSpacingScale <= 0.0f
            || !std::isfinite(settings.blockerSearchRadius) || settings.blockerSearchRadius <= 0.0f)
            return false;

        auto& shadow = m_Impl->ShadowSettings;
        shadow.pcss = settings.pcss;
        shadow.shadowDistance = settings.shadowDistance;
        shadow.pcfSampleCount = settings.pcfSampleCount;
        shadow.blockerSampleCount = settings.blockerSampleCount;
        shadow.legacyPcf25 = settings.legacyPcf25;
        shadow.pcfTapSpacingScale = settings.pcfTapSpacingScale;
        shadow.blockerSearchRadiusWorld = settings.blockerSearchRadius;
        return true;
    }

    ShadowFilterSettings DX11Renderer::GetShadowFilterSettings() const
    {
        const auto& shadow = m_Impl->ShadowSettings;
        ShadowFilterSettings settings;
        settings.pcss = shadow.pcss;
        settings.shadowDistance = shadow.shadowDistance;
        settings.pcfSampleCount = shadow.pcfSampleCount;
        settings.blockerSampleCount = shadow.blockerSampleCount;
        settings.legacyPcf25 = shadow.legacyPcf25;
        settings.pcfTapSpacingScale = shadow.pcfTapSpacingScale;
        settings.blockerSearchRadius = shadow.blockerSearchRadiusWorld;
        return settings;
    }

    void DX11Renderer::SetRenderView(const RenderView& view)
    {
        m_Impl->QueuedScene.camera = view;
    }

    void DX11Renderer::AddRenderItem(const RenderItem& item)
    {
        m_Impl->QueuedScene.items.push_back(item);
    }

    void DX11Renderer::AddLight(const RenderLight& light)
    {
        m_Impl->QueuedScene.lights.push_back(light);
    }

    void DX11Renderer::SetFog(const RenderFog& fog)
    {
        m_Impl->QueuedScene.fog = fog;
    }

    void DX11Renderer::SetSkybox(const RenderSkybox& skybox)
    {
        m_Impl->QueuedScene.skybox = skybox;
    }

    void DX11Renderer::SetAmbient(const Vector3& color, float intensity)
    {
        m_Impl->QueuedScene.ambientColor = color;
        m_Impl->QueuedScene.ambientIntensity = intensity;
    }

    MaterialResourceId DX11Renderer::CreateMaterial(const MaterialDesc& desc, TextureResourceId baseColorTexture,
        TextureResourceId ormTexture, TextureResourceId normalTexture,
        TextureResourceId occlusionTexture, TextureResourceId emissiveTexture)
    {
        if (!m_Impl->Initialized)
        {
            m_Impl->LastError = "CreateMaterial requires an initialized renderer";
            CN_LOG_RENDERER("Error", m_Impl->LastError);
            return {};
        }
        return m_Impl->Resources.CreateMaterial(desc, baseColorTexture, ormTexture, normalTexture,
            occlusionTexture, emissiveTexture, m_Impl->LastError);
    }

    MaterialDesc DX11Renderer::GetMaterial(MaterialResourceId material) const
    {
        if (const auto* resource = m_Impl->Resources.GetMaterial(material)) return resource->Desc;
        return {}; // Default white material, including nonexistent or stale IDs.
    }

    void DX11Renderer::DestroyMaterial(MaterialResourceId material)
    {
        m_Impl->Resources.DestroyMaterial(material);
    }

    bool DX11Renderer::IsMaterialValid(MaterialResourceId material) const
    {
        return m_Impl->Resources.IsMaterialValid(material);
    }

    TextureResourceId DX11Renderer::CreateTexture(uint32_t width, uint32_t height, TextureFormat format, std::span<const uint8_t> pixels)
    {
        return m_Impl->Resources.CreateTexture(m_Impl->Device.Get(), m_Impl->Context.Get(),
            width, height, format, pixels, m_Impl->LastError);
    }

    void DX11Renderer::SetTestNormalTexture(TextureResourceId texture)
    {
        m_Impl->TestNormalTexture = m_Impl->Resources.IsTextureValid(texture) ? texture : TextureResourceId{};
    }

    bool DX11Renderer::IsTextureValid(TextureResourceId texture) const
    {
        return m_Impl->Resources.IsTextureValid(texture);
    }

    void DX11Renderer::DestroyTexture(TextureResourceId texture)
    {
        if (!IsTextureValid(texture)) return;
        ID3D11ShaderResourceView* unbound = nullptr;
        if (m_Impl->TestNormalTexture == texture)
        {
            m_Impl->TestNormalTexture = {};
            m_Impl->Context->PSSetShaderResources(3, 1, &unbound);
        }
        m_Impl->Context->PSSetShaderResources(0, 1, &unbound);
        m_Impl->Resources.DestroyTexture(texture);
    }

    CubemapResourceId DX11Renderer::CreateCubemap(uint32_t faceSize, uint32_t mipLevels, TextureFormat format, std::span<const uint8_t> pixels)
    {
        return m_Impl->Resources.CreateCubemap(m_Impl->Device.Get(), m_Impl->Context.Get(),
            faceSize, mipLevels, format, pixels, m_Impl->LastError);
    }

    bool DX11Renderer::IsCubemapValid(CubemapResourceId cubemap) const
    {
        return m_Impl->Resources.IsCubemapValid(cubemap);
    }

    void DX11Renderer::DestroyCubemap(CubemapResourceId cubemap)
    {
        m_Impl->Resources.DestroyCubemap(cubemap);
    }

    bool DX11Renderer::BeginFrame()
    {
        return m_Impl->BeginFrame();
    }

    void DX11Renderer::DrawScene()
    {
        DrawScene(m_Impl->QueuedScene);
        m_Impl->QueuedScene = {};
    }

    void DX11Renderer::DrawScene(const RenderScene& scene)
    {
        m_Impl->ExecuteScene(scene);
    }

    void DX11Renderer::DrawMesh(MeshResourceId mesh, const float* world, uint32_t submeshIndex, MaterialResourceId material)
    {
        m_Impl->DrawMesh(mesh, world, submeshIndex, material);
    }

    void DX11Renderer::EndFrame()
    {
        m_Impl->EndFrame();
    }

    void DX11Renderer::SetSceneSize(uint32_t width, uint32_t height)
    {
        m_Impl->RequestedSceneWidth = width && height ? width : 0;
        m_Impl->RequestedSceneHeight = width && height ? height : 0;
    }

    void DX11Renderer::SetOverlay(std::function<void()> overlay)
    {
        m_Impl->Overlay = std::move(overlay);
    }

    void* DX11Renderer::GetNativeDevice() const
    {
        return m_Impl->Device.Get();
    }

    void* DX11Renderer::GetNativeContext() const
    {
        return m_Impl->Context.Get();
    }

    void* DX11Renderer::GetSceneTexture() const
    {
        return m_Impl->DisplayShaderResourceView.Get();
    }

    uint32_t DX11Renderer::GetWidth() const
    {
        return m_Impl->Width;
    }

    uint32_t DX11Renderer::GetHeight() const
    {
        return m_Impl->Height;
    }

    const std::string& DX11Renderer::GetLastError() const
    {
        return m_Impl->LastError;
    }
}
