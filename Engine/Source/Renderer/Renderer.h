#pragma once

#include "Asset/AssetId.h"
#include "DX11Renderer/Scene/RenderScene.h"
#include "Math/Matrix4.h"
#include "Math/Vector3.h"
#include "DX11Renderer/Data/RendererDebugSettings.h"
#include "DX11Renderer/Data/RendererDebugStats.h"
#include "DX11Renderer/Data/ShadowFilterSettings.h"
#include "DX11Renderer/Data/PostProcessSettings.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Canape
{
    class AssetManager;
    class DX11Renderer;
    class GpuResourceRegistry;
    class Scene;

    class Renderer final
    {
    public:
        Renderer();
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        bool Initialize(void* nativeWindow, uint32_t width, uint32_t height);

        void Render(Scene* scene, const AssetManager& assets);

        void SetViewOverride(const RenderView& view) { m_ViewOverride = view; }
        void ClearViewOverride() { m_ViewOverride.reset(); }

        void SetSceneSize(uint32_t width, uint32_t height);
        void SetOverlay(std::function<void()> overlay);
        void* GetNativeDevice() const;
        void* GetNativeContext() const;
        void* GetSceneTexture() const;

        static constexpr bool IsDebugAvailable() { return CANAPE_RENDER_DEBUG != 0; }
        const RendererDebugSettings& GetDebugSettings() const { return m_DebugSettings; }
        RendererDebugStats GetDebugStats() const;
        void ResetDebugSettings();

        void SetDebugEnabled(bool enabled);
        void SetDebugViewMode(DebugViewMode mode);
        void SetDebugDepthRange(float depthMin, float depthMax);
        void SetWireframe(bool wireframe);
        void SetGpuTimingCapture(bool capture);
        void SetShadowCullMode(ShadowCullMode mode);
        void SetShadowSlopeBias(float bias);
        void SetShadowReceiverBias(float bias);
        void SetShadowPcf(bool pcf);

        PostProcessSettings GetPostProcessSettings() const;
        void SetToneMappingEnabled(bool enabled);
        void ResetPostProcessSettings();

        const ShadowFilterSettings& GetShadowFilterSettings() const { return m_ShadowFilterSettings; }
        void ResetShadowFilterSettings();

        void SetPcss(bool pcss);
        void SetShadowDistance(float distance);
        void SetPcfSampleCount(uint32_t count);
        void SetBlockerSampleCount(uint32_t count);
        void SetLegacyPcf25(bool legacy);
        void SetPcfTapSpacingScale(float scale);
        void SetBlockerSearchRadius(float radius);

        void AddRenderItem(MeshAssetId mesh, uint32_t submeshIndex, MaterialAssetId material, const Matrix4& world);
        void AddLight(const RenderLight& light);
        void SetFog(const RenderFog& fog);
        void SetSkybox(CubemapAssetId cubemap, const RenderSkybox& skybox);
        void SetAmbient(const Vector3& color, float intensity);

        GpuResourceRegistry& GetRegistry() { return *m_Registry; }

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;

        const std::string& GetLastError() const;

    private:
        std::unique_ptr<DX11Renderer> m_DX11;
        std::unique_ptr<GpuResourceRegistry> m_Registry;
        std::optional<RenderView> m_ViewOverride;
        bool m_WarnedNoCamera = false;
        bool m_TestNormalAttempted = false;
        RendererDebugSettings m_DebugSettings{};
        ShadowFilterSettings m_ShadowFilterSettings{};

        void ApplyDebugSettings();
        void ApplyShadowFilterSettings();
    };
}
