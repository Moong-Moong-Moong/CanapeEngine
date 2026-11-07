#include "Renderer/Renderer.h"
#include "Renderer/GpuResourceRegistry.h"
#include "DX11Renderer/DX11Renderer.h"
#include "Asset/AssetManager.h"
#include "Asset/Texture/TextureImporter.h"
#include "CNDebug/DebugLog.h"
#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/Scene.h"

namespace Canape
{
    Renderer::Renderer()
        : m_DX11(std::make_unique<DX11Renderer>()),
        m_Registry(std::make_unique<GpuResourceRegistry>(*m_DX11))
    {
    }

    Renderer::~Renderer() = default;

    bool Renderer::Initialize(void* nativeWindow, uint32_t width, uint32_t height)
    {
        if (!m_DX11->Initialize(nativeWindow, width, height)) return false;
        m_TestNormalAttempted = false;
        m_DX11->SetDebugSettings(m_DebugSettings);
        m_DebugSettings = m_DX11->GetDebugSettings();
        m_ShadowFilterSettings = m_DX11->GetShadowFilterSettings();
        return true;
    }

    RendererDebugStats Renderer::GetDebugStats() const
    {
        return m_DX11->GetDebugStats();
    }

    void Renderer::ResetDebugSettings()
    {
        m_DebugSettings = {};
        ApplyDebugSettings();
    }

    void Renderer::SetDebugEnabled(bool enabled)
    {
        m_DebugSettings.enabled = enabled;
        ApplyDebugSettings();
    }

    void Renderer::SetDebugViewMode(DebugViewMode mode)
    {
        m_DebugSettings.viewMode = mode;
        ApplyDebugSettings();
    }

    void Renderer::SetDebugDepthRange(float depthMin, float depthMax)
    {
        m_DebugSettings.depthMin = depthMin;
        m_DebugSettings.depthMax = depthMax;
        ApplyDebugSettings();
    }

    void Renderer::SetWireframe(bool wireframe)
    {
        m_DebugSettings.wireframe = wireframe;
        ApplyDebugSettings();
    }

    void Renderer::SetGpuTimingCapture(bool capture)
    {
        m_DebugSettings.captureGpuTimings = capture;
        ApplyDebugSettings();
    }

    void Renderer::SetShadowCullMode(ShadowCullMode mode)
    {
        m_DebugSettings.shadowCullMode = mode;
        ApplyDebugSettings();
    }

    void Renderer::SetShadowSlopeBias(float bias)
    {
        m_DebugSettings.shadowSlopeBias = bias;
        ApplyDebugSettings();
    }

    void Renderer::SetShadowReceiverBias(float bias)
    {
        m_DebugSettings.shadowReceiverBias = bias;
        ApplyDebugSettings();
    }

    void Renderer::SetShadowPcf(bool pcf)
    {
        m_DebugSettings.shadowPcf = pcf;
        ApplyDebugSettings();
    }

    void Renderer::ApplyDebugSettings()
    {
        // Apply before BeginFrame so GPU query capture starts on this frame.
        m_DX11->SetDebugSettings(m_DebugSettings);
        m_DebugSettings = m_DX11->GetDebugSettings();
    }

    PostProcessSettings Renderer::GetPostProcessSettings() const
    {
        return m_DX11->GetPostProcessSettings();
    }

    void Renderer::SetToneMappingEnabled(bool enabled)
    {
        auto settings = m_DX11->GetPostProcessSettings();
        settings.toneMappingEnabled = enabled;
        m_DX11->SetPostProcessSettings(settings);
    }

    void Renderer::ResetPostProcessSettings()
    {
        m_DX11->SetPostProcessSettings({});
    }

    void Renderer::ResetShadowFilterSettings()
    {
        m_ShadowFilterSettings = {};
        ApplyShadowFilterSettings();
    }

    void Renderer::SetPcss(bool pcss)
    {
        m_ShadowFilterSettings.pcss = pcss;
        ApplyShadowFilterSettings();
    }

    void Renderer::SetShadowDistance(float distance)
    {
        m_ShadowFilterSettings.shadowDistance = distance;
        ApplyShadowFilterSettings();
    }

    void Renderer::SetPcfSampleCount(uint32_t count)
    {
        m_ShadowFilterSettings.pcfSampleCount = count;
        ApplyShadowFilterSettings();
    }

    void Renderer::SetBlockerSampleCount(uint32_t count)
    {
        m_ShadowFilterSettings.blockerSampleCount = count;
        ApplyShadowFilterSettings();
    }

    void Renderer::SetLegacyPcf25(bool legacy)
    {
        m_ShadowFilterSettings.legacyPcf25 = legacy;
        ApplyShadowFilterSettings();
    }

    void Renderer::SetPcfTapSpacingScale(float scale)
    {
        m_ShadowFilterSettings.pcfTapSpacingScale = scale;
        ApplyShadowFilterSettings();
    }

    void Renderer::SetBlockerSearchRadius(float radius)
    {
        m_ShadowFilterSettings.blockerSearchRadius = radius;
        ApplyShadowFilterSettings();
    }

    void Renderer::ApplyShadowFilterSettings()
    {
        m_DX11->SetShadowFilterSettings(m_ShadowFilterSettings);
        m_ShadowFilterSettings = m_DX11->GetShadowFilterSettings();
    }

    void Renderer::Render(Scene* scene, const AssetManager& assets)
    {
        if (!m_TestNormalAttempted)
        {
            m_TestNormalAttempted = true;
            // Temporary renderer-wide input test; material routing comes later.
            const auto file = AssetManager::GetAssetFile("Content/Texture/Player_Normal.png");
            std::string error;
            if (auto normal = TextureImporter::LoadTexture(file, &error))
            {
                const TextureResourceId texture = m_DX11->CreateTexture(normal->GetWidth(), normal->GetHeight(),
                    TextureFormat::RGBA8_UNORM, normal->GetPixels());
                if (texture.IsValid())
                {
                    m_DX11->SetTestNormalTexture(texture);
                    CN_LOG_RENDERER("NormalMap", "Player normal map loaded as linear RGBA8: ", file.string());
                }
                else
                    CN_LOG_RENDERER("NormalMap", "Player normal map GPU upload failed: ", m_DX11->GetLastError());
            }
            else
                CN_LOG_RENDERER("NormalMap", "Player normal map load failed: ", error);
        }

        if (!m_DX11->BeginFrame())
        {
            return;
        }

        CameraComponent* camera = scene ? scene->GetMainCamera() : nullptr;

        if (m_ViewOverride)
        {
            m_DX11->SetRenderView(*m_ViewOverride);
            if (scene)
            {
                scene->BuildRenderScene(assets, *this);
            }
        }

        else if (!camera || !camera->IsEnabled())
        {
            if (!m_WarnedNoCamera)
            {
                CN_LOG_RENDERER("Renderer", "메인 카메라 없음");
                m_WarnedNoCamera = true;
            }
        }

        else
        {
            m_WarnedNoCamera = false;

            const float aspectRatio = static_cast<float>(m_DX11->GetWidth()) / static_cast<float>(m_DX11->GetHeight());
            RenderView view;
            view.view = camera->GetViewMatrix();
            view.projection = camera->GetProjectionMatrix(aspectRatio);
            view.cameraPosition = camera->GetTransform()->GetPosition();
            m_DX11->SetRenderView(view);

            scene->BuildRenderScene(assets, *this);
        }

        m_DX11->DrawScene();
        m_DX11->EndFrame();
    }

    void Renderer::AddRenderItem(MeshAssetId mesh, uint32_t submeshIndex, MaterialAssetId material, const Matrix4& world)
    {
        const MeshResourceId meshResource = m_Registry->GetResourceId(mesh);
        if (!meshResource.IsValid())
        {
            return;
        }

        RenderItem item;
        item.mesh = meshResource;
        item.submeshIndex = submeshIndex;
        item.material = m_Registry->GetResourceId(material);
        item.world = world;
        m_DX11->AddRenderItem(item);
    }

    void Renderer::AddLight(const RenderLight& light)
    {
        m_DX11->AddLight(light);
    }

    void Renderer::SetFog(const RenderFog& fog)
    {
        m_DX11->SetFog(fog);
    }

    void Renderer::SetSkybox(CubemapAssetId cubemap, const RenderSkybox& skybox)
    {
        RenderSkybox resolved = skybox;
        resolved.cubemap = m_Registry->GetResourceId(cubemap);
        m_DX11->SetSkybox(resolved);
    }

    void Renderer::SetAmbient(const Vector3& color, float intensity)
    {
        m_DX11->SetAmbient(color, intensity);
    }

    void Renderer::SetSceneSize(uint32_t width, uint32_t height)
    {
        m_DX11->SetSceneSize(width, height);
    }

    void Renderer::SetOverlay(std::function<void()> overlay)
    {
        m_DX11->SetOverlay(std::move(overlay));
    }

    void* Renderer::GetNativeDevice() const
    {
        return m_DX11->GetNativeDevice();
    }

    void* Renderer::GetNativeContext() const
    {
        return m_DX11->GetNativeContext();
    }

    void* Renderer::GetSceneTexture() const
    {
        return m_DX11->GetSceneTexture();
    }

    uint32_t Renderer::GetWidth() const
    {
        return m_DX11->GetWidth();
    }

    uint32_t Renderer::GetHeight() const
    {
        return m_DX11->GetHeight();
    }

    const std::string& Renderer::GetLastError() const
    {
        return m_DX11->GetLastError();
    }
}
