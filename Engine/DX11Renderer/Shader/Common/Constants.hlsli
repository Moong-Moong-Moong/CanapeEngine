#ifndef CANAPE_CONSTANTS_HLSLI
#define CANAPE_CONSTANTS_HLSLI

// Row-major packing is selected by the compiler. CPU uploads are not transposed.
cbuffer ObjectBuffer : register(b0)
{
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

// Linear RGB and alpha. Layout matches the 32-byte CPU MaterialConstants.
cbuffer MaterialBuffer : register(b1)
{
    float4 BaseColor;
    uint MaterialAlphaMode;
    float AlphaCutoff;
    uint HasBaseColorTexture;
    float MaterialPadding;
};

// Per view. Layout matches the 208-byte CPU ViewConstants.
cbuffer ViewBuffer : register(b2)
{
    float4x4 View;
    float4x4 Projection;
    float4x4 ViewProjection;
    float3 CameraPosition;
    float ViewPadding;
};

// Initial fixed capacities. Keep these in sync with DX11Renderer::Impl.
static const uint MAX_DIRECTIONAL_LIGHTS = 4;
static const uint MAX_LOCAL_LIGHTS = 64;
static const uint MAX_SHADOW_VIEWS = 64;
static const uint INVALID_SHADOW_VIEW = 0xffffffffu;
static const uint LOCAL_LIGHT_POINT = 0;
static const uint LOCAL_LIGHT_SPOT = 1;

struct DirectionalLightGpu
{
    float3 Direction;
    uint ShadowViewOffset;
    float3 ColorIntensity;
    uint ShadowViewCount;
};

struct LocalLightGpu
{
    float3 Position;
    float Range;
    float3 Direction;
    float OuterAngleCos;
    float3 ColorIntensity;
    float InnerAngleCos;
    uint Type;
    uint ShadowViewOffset;
    uint ShadowViewCount;
    uint Padding;
};

// Uploaded per frame/scene and bound to PS b3. CPU layout: 4240 bytes.
cbuffer LightBuffer : register(b3)
{
    uint DirectionalCount;
    uint LocalCount;
    uint2 LightPadding;
    DirectionalLightGpu DirectionalLights[MAX_DIRECTIONAL_LIGHTS];
    LocalLightGpu LocalLights[MAX_LOCAL_LIGHTS];
};

// Ambient is used by mesh shading; fog data remains available for future shading.
// Layout matches the 48-byte CPU SceneConstants.
cbuffer SceneBuffer : register(b4)
{
    float3 AmbientColor;
    float AmbientIntensity;
    float3 FogColor;
    float FogDensity;
    float FogStartDistance;
    float FogEndDistance;
    uint FogMode;
    float ScenePadding;
};

struct ShadowViewGpu
{
    float4x4 LightViewProjection;
    float DepthBias;
    uint MapIndex;
    float NearPlane;
    float FarPlane;
};

// Uploaded per frame/scene; VS/PS b5. CPU layout: 5136 bytes.
// Currently one Directional view at index 0, with Texture2D depth at t1 / sampler s1.
// No CSM: Directional/Spot use one view; Point uses six in +X,-X,+Y,-Y,+Z,-Z order.
// MapIndex selects a 2D-array slice or cube, according to light type.
// The six views of one Point light share the same cube index.
cbuffer ShadowBuffer : register(b5)
{
    uint ShadowViewCount;
    uint3 ShadowPadding;
    ShadowViewGpu ShadowViews[MAX_SHADOW_VIEWS];
};

#endif
