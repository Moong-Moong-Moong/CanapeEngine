#ifndef CANAPE_SHADOW_SHADOWCONSTANTS_HLSLI
#define CANAPE_SHADOW_SHADOWCONSTANTS_HLSLI

// Keep capacities and layouts in sync with Data/ShaderConstants.h.
static const uint MAX_SHADOW_VIEWS = 64;
static const uint INVALID_SHADOW_VIEW = 0xffffffffu;

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
    uint ShadowPcfEnabled;
    uint2 ShadowPadding;
    ShadowViewGpu ShadowViews[MAX_SHADOW_VIEWS];
};

// DX11Renderer-local settings. ShadowBuffer b5 and its public layout are unchanged.
cbuffer PcssBuffer : register(b7)
{
    float2 ShadowOrthoSize;
    float TanLightAngularRadius;
    float BlockerSearchRadiusWorld;
    float MinFilterRadiusWorld;
    float MaxFilterRadiusWorld;
    uint ShadowFilterMode;
    uint PcssPcfSampleCount;
    float PcssPcfTapSpacingScale;
    uint BlockerSearchSampleCount;
    float2 PcssPadding;
};

Texture2D<float> DirectionalShadowMap : register(t1);
SamplerComparisonState ShadowSampler : register(s1);

#endif
