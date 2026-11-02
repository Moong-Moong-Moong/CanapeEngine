#ifndef CANAPE_SHADOW_SHADOWSAMPLING_HLSLI
#define CANAPE_SHADOW_SHADOWSAMPLING_HLSLI

#include "../Common/Constants.hlsli"
#include "ShadowConstants.hlsli"
#include "PCSS.hlsli"

float DirectionalShadowVisibility(float3 worldPosition, DirectionalLightGpu light)
{
    if (light.ShadowViewCount == 0 || light.ShadowViewOffset >= min(ShadowViewCount, MAX_SHADOW_VIEWS))
        return 1.0f;
    ShadowViewGpu shadow = ShadowViews[light.ShadowViewOffset];
    if (shadow.MapIndex != 0) return 1.0f;
    float4 clipPosition = mul(float4(worldPosition, 1.0f), shadow.LightViewProjection);
    if (clipPosition.w <= 0.0f) return 1.0f;
    float3 ndc = clipPosition.xyz / clipPosition.w;
    if (any(ndc.xy < -1.0f) || any(ndc.xy > 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f)
        return 1.0f;
    // D3D NDC z is [0,1]; texture V points down, unlike NDC Y.
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float compareDepth = ndc.z - shadow.DepthBias;
    if (ShadowPcfEnabled == 0)
        return DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, uv, compareDepth);
    if (ShadowFilterMode == 2 || ShadowFilterMode == 3)
        return FilterShadowPcss(uv, ndc.z, shadow);

    uint shadowWidth, shadowHeight;
    DirectionalShadowMap.GetDimensions(shadowWidth, shadowHeight);
    float2 texelSize = 1.0f / float2(shadowWidth, shadowHeight);
    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
            visibility += DirectionalShadowMap.SampleCmpLevelZero(
                ShadowSampler, uv + float2(x, y) * texelSize, compareDepth);
    }
    return visibility / 9.0f;
}

#endif
