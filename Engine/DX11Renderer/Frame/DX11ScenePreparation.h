#pragma once
#include "DX11Renderer/Data/ShaderConstants.h"
#include "DX11Renderer/Data/RendererDebugSettings.h"
#include "DX11Renderer/Resource/DX11ResourceCache.h"
#include "DX11Renderer/Pipeline/DX11PipelineState.h"

#include <vector>

namespace Canape
{
    // Existing camera-fitted directional shadow settings; defaults are unchanged.
    struct DirectionalShadowSettings
    {
        bool enabled = true;
        uint32_t resolution = 4096*2;
        bool pcf = true; // 3x3 taps with hardware comparison filtering; false selects a single comparison.
        float shadowDistance = 25.0f; // Camera-space extent covered by this shadow map.
        Vector3 center{}; // Legacy fixed-frustum values; fitted projection uses the camera.
        float width = 40.0f;
        float height = 40.0f;
        float lightDistance = 80.0f;
        float nearPlane = 0.1f;
        float farPlane = 200.0f;
        float depthBias = DefaultShadowReceiverBias; // Receiver comparison bias.
        bool pcss = true;
        float lightAngularRadius = 0.02f; // Radians; directional light's virtual angular radius.
        float blockerSearchRadiusWorld = 0.125f;
        float minFilterRadiusWorld = 0.0f; // One shadow texel is the shader-side floor.
        float maxFilterRadiusWorld = 0.4f / 1.5f; // Base penumbra cap; final taps are also clamped to the search radius.
        float pcfTapSpacingScale = 1.0f; // Diagnostic multiplier after penumbra estimation.
        uint32_t pcfSampleCount = 64;
        uint32_t blockerSampleCount = 128;
        bool legacyPcf25 = true; // Original 25 offsets remain available for comparison.
    };
    struct PreparedMaterial
    {
        MaterialDesc Description;
        ShaderConstants::MaterialConstants Constants;
        ID3D11ShaderResourceView* Texture = nullptr; // Borrowed from the resource cache.
        ID3D11ShaderResourceView* ORMTexture = nullptr;
    };
    struct PreparedLighting
    {
        ShaderConstants::LightConstants Lights{};
        ShaderConstants::ShadowConstants Shadows{};
        ShaderConstants::PcssConstants Pcss{};
        uint32_t ShadowDirectionalIndex = ShaderConstants::InvalidShadowView;
        bool ShadowActive = false;
    };
    struct PreparedMeshDraw
    {
        const DX11MeshResource* Mesh = nullptr;
        uint32_t SubmeshIndex = 0;
        ShaderConstants::ObjectConstants Object{};
        PreparedMaterial Material;
        float Depth = 0.0f;
    };
    struct PreparedSkybox
    {
        ID3D11ShaderResourceView* Texture = nullptr;
        ShaderConstants::SkyboxViewConstants Constants{};
    };
    struct PreparedScene
    {
        ShaderConstants::ViewConstants View{};
        ShaderConstants::SceneConstants Scene{};
        std::vector<PreparedMeshDraw> Draws;
        std::vector<size_t> Opaque, Masked, Transparent, ShadowCasters;
        PreparedSkybox Skybox;
    };
    struct PreparedFinalOutput
    {
        DX11PipelineSelection Pipeline;
        ID3D11ShaderResourceView* Texture = nullptr;
        bool DisplayDepth = false;
#if CANAPE_RENDER_DEBUG
        ShaderConstants::DepthDisplayConstants DepthDisplay{};
#endif
    };
    namespace ScenePreparation
    {
        bool PrepareMeshDraw(const DX11ResourceCache& resources, MeshResourceId mesh, const float* world,
            uint32_t submeshIndex, MaterialResourceId material, PreparedMeshDraw& result, std::string& error);
        PreparedScene PrepareScene(const RenderScene& scene, const DX11ResourceCache& resources, std::string& error);
        PreparedSkybox PrepareSkybox(const RenderSkybox& skybox, const ShaderConstants::ViewConstants& view,
            const DX11ResourceCache& resources);
        PreparedFinalOutput PrepareFinalOutput(const DX11PipelineState& pipelines,
            ID3D11ShaderResourceView* sceneColor, ID3D11ShaderResourceView* depth,
            ID3D11ShaderResourceView* shadow, bool shadowActive, const RendererDebugSettings* debug);
        ShaderConstants::ViewConstants BuildViewConstants(const float* view, const float* projection);
        ShaderConstants::SceneConstants BuildSceneConstants(const RenderScene& scene);
        ShaderConstants::ObjectConstants BuildObjectConstants(const float* world);
        PreparedMaterial PrepareMaterial(const DX11ResourceCache& resources, MaterialResourceId material);
        ShaderConstants::LightConstants BuildLightConstants(std::span<const RenderLight> lights,
            uint32_t* shadowDirectionalIndex = nullptr);
        bool BuildFrustumBoundingSphere(const ShaderConstants::ViewConstants& camera, float shadowDistance, Vector3& center,
            float& radius, double& fittedRadius);
        bool BuildDirectionalShadowView(const ShaderConstants::ViewConstants& view, uint32_t resolution,
            const Vector3& direction, const DirectionalShadowSettings& settings,
            ShaderConstants::ShadowViewGpu& output, ShaderConstants::PcssConstants& pcssOutput);
        PreparedLighting PrepareLights(std::span<const RenderLight> lights, const DirectionalShadowSettings& settings);
        void PrepareShadow(PreparedLighting& lighting, const ShaderConstants::ViewConstants& view,
            const DirectionalShadowSettings& settings, uint32_t resolution);
    }
}
