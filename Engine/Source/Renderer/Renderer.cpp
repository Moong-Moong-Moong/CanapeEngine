#include "Renderer/Renderer.h"
#include "Renderer/GpuResourceRegistry.h"
#include "DX11Renderer/DX11Renderer.h"
#include "Asset/AssetManager.h"
#include "CNDebug/DebugLog.h"
#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/Scene.h"
#include "Simulation/SimulationScene.h"

namespace Canape
{
    Renderer::Renderer()
        : m_DX11(std::make_unique<DX11Renderer>()),
        m_Registry(std::make_unique<GpuResourceRegistry>(*m_DX11))
    {
    }

    Renderer::~Renderer() = default;

    bool Renderer::Initialize(void* nativeWindow, uint32_t width, uint32_t height, bool headless)
    {
        if (!m_DX11->Initialize(nativeWindow, width, height, headless)) return false;
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

    void Renderer::SetUIDrawList(const UIDrawList* list)
    {
        m_DX11->SetUIDrawList(list);
    }

    TextureResourceId Renderer::CreateTexture(uint32_t width, uint32_t height, TextureFormat format, std::span<const uint8_t> pixels)
    {
        return m_DX11->CreateTexture(width, height, format, pixels);
    }

    bool Renderer::UpdateTexture(TextureResourceId texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
        std::span<const uint8_t> pixels)
    {
        return m_DX11->UpdateTexture(texture, x, y, width, height, pixels);
    }

    void Renderer::DestroyTexture(TextureResourceId texture)
    {
        m_DX11->DestroyTexture(texture);
    }

    std::optional<RenderView> Renderer::ResolveView(Scene* scene) const
    {
        if (m_ViewOverride)
        {
            return m_ViewOverride;
        }
        CameraComponent* camera = scene ? scene->GetMainCamera() : nullptr;
        const uint32_t width = m_DX11->GetWidth();
        const uint32_t height = m_DX11->GetHeight();
        if (!camera || !camera->IsEnabled() || width == 0 || height == 0)
        {
            return std::nullopt;
        }
        const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        RenderView view;
        view.view = camera->GetViewMatrix();
        view.projection = camera->GetProjectionMatrix(aspectRatio);
        view.cameraPosition = camera->GetTransform()->GetWorldPosition();
        return view;
    }

    void Renderer::Render(Scene* scene, const AssetManager& assets, Simulator& simulator)
    {
        if (!m_DX11->BeginFrame())
        {
            // 빌린 UI 목록을 다음 프레임까지 들고 있지 않는다.
            m_DX11->SetUIDrawList(nullptr);
            return;
        }

        bool sceneBuilt = false;

        const std::optional<RenderView> view = ResolveView(scene);
        if (!view)
        {
            if (!m_WarnedNoCamera)
            {
                CN_LOG_RENDERER("Renderer", "메인 카메라 없음");
                m_WarnedNoCamera = true;
            }
        }

        else
        {
            if (!m_ViewOverride)
            {
                m_WarnedNoCamera = false;
            }
            m_DX11->SetRenderView(*view);
            if (scene)
            {
                scene->BuildRenderScene(assets, *this);

                if (m_EditorSubmit)
                {
                    m_EditorSubmit(*scene, *this);
                }

                sceneBuilt = true;
            }
        }

        if (sceneBuilt)
        {
            if (SimulationScene* simulation = scene->GetSimulation())
            {
                m_DX11->SetAfterOpaqueCallback(
                    [simulation, &simulator]
                    {
                        simulation->RenderAfterOpaque(simulator);
                    }
                );
                m_DX11->SetAfterTransparentCallback(
                    [simulation, &simulator]
                    {
                        simulation->RenderAfterTransparent(simulator);
                    }
                );
                m_DX11->SetMirrorCallback(
                    [simulation, &simulator]
                    {
                        simulation->RenderMirror(simulator);
                    }
                );
            }
        }
        m_DX11->DrawScene();
        m_DX11->SetAfterOpaqueCallback(nullptr);
        m_DX11->SetAfterTransparentCallback(nullptr);
        m_DX11->SetMirrorCallback(nullptr);
        m_DX11->EndFrame();
    }

    void Renderer::AddRenderItem(MeshAssetId mesh, uint32_t submeshIndex, MaterialAssetId material, const Matrix4& world,
        uint64_t objectId, bool outlineEnabled, const OutlineStyle& outlineStyle, bool castShadow)
    {
        const MeshResourceId meshResource = m_Registry->GetResourceId(mesh);
        if (!meshResource.IsValid())
        {
            return;
        }

        const bool selected = objectId != 0 && m_SelectionOutline.contains(objectId);
        MeshItem item;
        item.outlineStyle = selected ? m_SelectionStyle : outlineStyle;
        item.objectId = objectId;
        item.outlineEnabled = outlineEnabled || selected;
        item.mesh = meshResource;
        item.submeshIndex = submeshIndex;
        item.material = m_Registry->GetResourceId(material);
        item.world = world;
        item.castShadow = castShadow;
        m_DX11->AddRenderItem(item);
    }

    uint32_t Renderer::AddBonePalette(std::span<const Matrix4> palette)
    {
        return m_DX11->AddBonePalette(palette);
    }

    void Renderer::AddSkinnedRenderItem(MeshAssetId mesh, uint32_t submeshIndex, MaterialAssetId material, const Matrix4& world,
        uint32_t paletteOffset, uint32_t paletteCount, uint64_t objectId, bool outlineEnabled, const OutlineStyle& outlineStyle)
    {
        const MeshResourceId meshResource = m_Registry->GetResourceId(mesh);
        if (!meshResource.IsValid())
        {
            return;
        }

        const bool selected = objectId != 0 && m_SelectionOutline.contains(objectId);
        SkinnedMeshItem item;
        item.outlineStyle = selected ? m_SelectionStyle : outlineStyle;
        item.objectId = objectId;
        item.outlineEnabled = outlineEnabled || selected;
        item.mesh = meshResource;
        item.submeshIndex = submeshIndex;
        item.material = m_Registry->GetResourceId(material);
        item.world = world;
        item.paletteOffset = paletteOffset;
        item.paletteCount = paletteCount;
        m_DX11->AddSkinnedRenderItem(item);
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

    void Renderer::SetMirrorPlaneHeight(float height)
    {
        m_DX11->SetMirrorPlaneHeight(height);
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

    void Renderer::RequestPick(uint32_t x, uint32_t y)
    {
        m_DX11->RequestPick(x, y);
    }

    bool Renderer::TakePickResult(uint64_t& objectId)
    {
        return m_DX11->TakePickResult(objectId);
    }

    void Renderer::SetSelectionOutline(std::vector<uint64_t> objectIds, const OutlineStyle& style)
    {
        m_SelectionOutline = std::unordered_set<uint64_t>(objectIds.begin(), objectIds.end());
        m_SelectionStyle = style;
    }

    void Renderer::SetEditorGridEnabled(bool enabled)
    {
        m_DX11->SetEditorGridEnabled(enabled);
    }

    bool Renderer::IsEditorGridEnabled() const
    {
        return m_DX11->IsEditorGridEnabled();
    }

    void Renderer::SetEditorGridPlaneVisible(EditorGridPlane plane, bool visible)
    {
        m_DX11->SetEditorGridPlaneVisible(plane, visible);
    }

    bool Renderer::IsEditorGridPlaneVisible(EditorGridPlane plane) const
    {
        return m_DX11->IsEditorGridPlaneVisible(plane);
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
