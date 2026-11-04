#pragma once

#include "DX11Renderer/Scene/RenderScene.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>

namespace Canape::ShaderConstants
{
    struct alignas(16) ObjectConstants
    {
        DirectX::XMFLOAT4X4 World;
        DirectX::XMFLOAT4X4 WorldInverseTranspose;
    };
    static_assert(sizeof(ObjectConstants) == 128);
    static_assert(offsetof(ObjectConstants, WorldInverseTranspose) == 64);

    struct alignas(16) ViewConstants
    {
        DirectX::XMFLOAT4X4 View{};
        DirectX::XMFLOAT4X4 Projection{};
        DirectX::XMFLOAT4X4 ViewProjection{};
        Vector3 CameraPosition{};
        float Padding = 0.0f;
    };
    static_assert(sizeof(ViewConstants) == 208);
    static_assert(offsetof(ViewConstants, Projection) == 64);
    static_assert(offsetof(ViewConstants, ViewProjection) == 128);
    static_assert(offsetof(ViewConstants, CameraPosition) == 192);

    // Matches Skybox.hlsl b6; shared by VS camera reconstruction and PS brightness.
    struct alignas(16) SkyboxViewConstants
    {
        DirectX::XMFLOAT4X4 InverseProjection{};
        DirectX::XMFLOAT4X4 InverseView{};
        float Brightness = 1.0f;
        float RotationCos = 1.0f;
        float RotationSin = 0.0f;
        float Padding = 0.0f;
    };
    static_assert(sizeof(SkyboxViewConstants) == 144);
    static_assert(alignof(SkyboxViewConstants) == 16);
    static_assert(offsetof(SkyboxViewConstants, InverseView) == 64);
    static_assert(offsetof(SkyboxViewConstants, Brightness) == 128);
    static_assert(offsetof(SkyboxViewConstants, RotationSin) == 136);

    // Initial fixed capacities. Keep these in sync with Common/Constants.hlsli
    // and Shadow/ShadowConstants.hlsli.
    inline constexpr uint32_t MaxDirectionalLights = 4;
    inline constexpr uint32_t MaxLocalLights = 64;
    inline constexpr uint32_t MaxShadowViews = 64;
    inline constexpr uint32_t InvalidShadowView = UINT32_MAX;

    enum class LocalLightType : uint32_t { Point = 0, Spot = 1 };

    struct alignas(16) DirectionalLightGpu
    {
        Vector3 Direction{ 0.0f, -1.0f, 0.0f }; // Normalized world travel direction.
        uint32_t ShadowViewOffset = InvalidShadowView;
        Vector3 ColorIntensity{}; // Linear color multiplied by intensity.
        uint32_t ShadowViewCount = 0; // No shadow: 0. Directional shadow: 1.
    };
    static_assert(sizeof(DirectionalLightGpu) == 32);
    static_assert(offsetof(DirectionalLightGpu, ShadowViewOffset) == 12);
    static_assert(offsetof(DirectionalLightGpu, ColorIntensity) == 16);
    static_assert(offsetof(DirectionalLightGpu, ShadowViewCount) == 28);

    struct alignas(16) LocalLightGpu
    {
        Vector3 Position{};
        float Range = 10.0f;
        Vector3 Direction{ 0.0f, -1.0f, 0.0f }; // Spot only; normalized.
        float OuterAngleCos = 1.0f; // Spot only; cosine of the half-angle.
        Vector3 ColorIntensity{};
        float InnerAngleCos = 1.0f;
        uint32_t Type = static_cast<uint32_t>(LocalLightType::Point);
        uint32_t ShadowViewOffset = InvalidShadowView;
        uint32_t ShadowViewCount = 0; // No shadow: 0. Spot: 1. Point cubemap: 6.
        uint32_t Padding = 0;
    };
    static_assert(sizeof(LocalLightGpu) == 64);
    static_assert(offsetof(LocalLightGpu, Range) == 12);
    static_assert(offsetof(LocalLightGpu, Direction) == 16);
    static_assert(offsetof(LocalLightGpu, OuterAngleCos) == 28);
    static_assert(offsetof(LocalLightGpu, ColorIntensity) == 32);
    static_assert(offsetof(LocalLightGpu, InnerAngleCos) == 44);
    static_assert(offsetof(LocalLightGpu, Type) == 48);
    static_assert(offsetof(LocalLightGpu, ShadowViewOffset) == 52);
    static_assert(offsetof(LocalLightGpu, ShadowViewCount) == 56);

    struct alignas(16) LightConstants
    {
        uint32_t DirectionalCount = 0;
        uint32_t LocalCount = 0;
        uint32_t Padding[2]{};
        DirectionalLightGpu DirectionalLights[MaxDirectionalLights]{};
        LocalLightGpu LocalLights[MaxLocalLights]{};
    };
    static_assert(offsetof(LightConstants, DirectionalLights) == 16);
    static_assert(offsetof(LightConstants, LocalLights) == 16 + 32 * MaxDirectionalLights);
    static_assert(sizeof(LightConstants) == 16 + 32 * MaxDirectionalLights + 64 * MaxLocalLights);

    struct alignas(16) ShadowViewGpu
    {
        DirectX::XMFLOAT4X4 LightViewProjection{}; // LightView * LightProjection.
        float DepthBias = 0.0f;
        uint32_t MapIndex = UINT32_MAX; // 2D-array slice or cube index, according to light type.
        float NearPlane = 0.1f;
        float FarPlane = 10.0f;
    };
    static_assert(sizeof(ShadowViewGpu) == 80);
    static_assert(offsetof(ShadowViewGpu, DepthBias) == 64);
    static_assert(offsetof(ShadowViewGpu, MapIndex) == 68);
    static_assert(offsetof(ShadowViewGpu, NearPlane) == 72);
    static_assert(offsetof(ShadowViewGpu, FarPlane) == 76);

    struct alignas(16) ShadowConstants
    {
        uint32_t ViewCount = 0;
        uint32_t PcfEnabled = 0;
        uint32_t Padding[2]{};
        // Directional/Spot use one view. Point uses six in +X,-X,+Y,-Y,+Z,-Z order.
        ShadowViewGpu Views[MaxShadowViews]{};
    };
    static_assert(offsetof(ShadowConstants, PcfEnabled) == 4);
    static_assert(offsetof(ShadowConstants, Views) == 16);
    static_assert(sizeof(ShadowConstants) == 16 + 80 * MaxShadowViews);
    static_assert(sizeof(LightConstants) <= 65536 && sizeof(ShadowConstants) <= 65536);

    // Shadow/ShadowConstants.hlsli b7. b5 remains unchanged for all existing shadow consumers.
    struct alignas(16) PcssConstants
    {
        DirectX::XMFLOAT2 OrthoSize{};
        float TanLightAngularRadius = 0.0f;
        float BlockerSearchRadiusWorld = 0.0f;
        float MinFilterRadiusWorld = 0.0f;
        float MaxFilterRadiusWorld = 0.0f;
        uint32_t FilterMode = 0; // 0: single, 1: 3x3 PCF, 2: Vogel PCSS, 3: legacy 25 PCSS.
        uint32_t PcfSampleCount = 64;
        float PcfTapSpacingScale = 1.0f;
        uint32_t BlockerSampleCount = 25;
        float Padding[2]{};
    };
    static_assert(sizeof(PcssConstants) == 48);
    static_assert(offsetof(PcssConstants, TanLightAngularRadius) == 8);
    static_assert(offsetof(PcssConstants, MinFilterRadiusWorld) == 16);
    static_assert(offsetof(PcssConstants, FilterMode) == 24);
    static_assert(offsetof(PcssConstants, PcfSampleCount) == 28);
    static_assert(offsetof(PcssConstants, PcfTapSpacingScale) == 32);
    static_assert(offsetof(PcssConstants, BlockerSampleCount) == 36);

    // FinalOutput.hlsl b8; b7 is already used by PCSS.
    inline constexpr uint32_t PostProcessSlot = 8;
    struct alignas(16) PostProcessConstants
    {
        uint32_t PostProcessingEnabled = 1;
        uint32_t ToneMappingEnabled = 1;
        uint32_t DebugViewEnabled = 0; // Derived from RendererDebugSettings, independent of effect toggles.
        uint32_t Padding = 0;
    };
    static_assert(sizeof(PostProcessConstants) == 16);
    static_assert(alignof(PostProcessConstants) == 16);
    static_assert(offsetof(PostProcessConstants, ToneMappingEnabled) == 4);
    static_assert(offsetof(PostProcessConstants, DebugViewEnabled) == 8);
    static_assert(offsetof(PostProcessConstants, Padding) == 12);

    struct alignas(16) SceneConstants
    {
        Vector3 AmbientColor{ 1.0f, 1.0f, 1.0f };
        float AmbientIntensity = 0.1f;
        Vector3 FogColor{ 0.5f, 0.5f, 0.5f };
        float FogDensity = 0.01f;
        float FogStartDistance = 0.0f;
        float FogEndDistance = 100.0f;
        uint32_t FogMode = static_cast<uint32_t>(Canape::FogMode::Disabled);
        float Padding = 0.0f;
    };
    static_assert(sizeof(SceneConstants) == 48);
    static_assert(offsetof(SceneConstants, AmbientIntensity) == 12);
    static_assert(offsetof(SceneConstants, FogColor) == 16);
    static_assert(offsetof(SceneConstants, FogDensity) == 28);
    static_assert(offsetof(SceneConstants, FogStartDistance) == 32);
    static_assert(offsetof(SceneConstants, FogEndDistance) == 36);
    static_assert(offsetof(SceneConstants, FogMode) == 40);

    struct MaterialConstants
    {
        Vector4 BaseColor;
        uint32_t Mode;
        float AlphaCutoff;
        uint32_t HasBaseColorTexture = 0;
        float Padding = 0.0f;
    };
    static_assert(sizeof(MaterialConstants) == 32);
    static_assert(offsetof(MaterialConstants, Mode) == 16 && offsetof(MaterialConstants, AlphaCutoff) == 20);
    static_assert(offsetof(MaterialConstants, HasBaseColorTexture) == 24);

#if CANAPE_RENDER_DEBUG
    struct alignas(16) DepthDisplayConstants
    {
        float Min;
        float Max;
        float Padding[2]{};
    };
    static_assert(sizeof(DepthDisplayConstants) == 16);
#endif
}
