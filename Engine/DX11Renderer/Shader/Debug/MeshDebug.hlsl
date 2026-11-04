#include "../Common/Constants.hlsli"
#include "../Common/MeshTypes.hlsli"
#include "../Common/MeshSurface.hlsli"
#include "../Common/Material.hlsli"
#include "../Shadow/ShadowSampling.hlsli"

float4 PSDebugShadowVisibility(VertexOutput input) : SV_TARGET
{
    float visibility = 1.0f;
    [loop]
    for (uint index = 0; index < min(DirectionalCount, MAX_DIRECTIONAL_LIGHTS); ++index)
    {
        if (DirectionalLights[index].ShadowViewCount != 0)
        {
            visibility = DirectionalShadowVisibility(input.WorldPosition, DirectionalLights[index]);
            break;
        }
    }
    return float4(visibility.xxx, GetDebugAlpha(input));
}

float4 PSDebugPcssFilterRadius(VertexOutput input) : SV_TARGET
{
    float radiusTexels = 0.0f;
    [loop]
    for (uint index = 0; index < min(DirectionalCount, MAX_DIRECTIONAL_LIGHTS); ++index)
    {
        DirectionalLightGpu light = DirectionalLights[index];
        if (light.ShadowViewCount == 0 || light.ShadowViewOffset >= min(ShadowViewCount, MAX_SHADOW_VIEWS))
            continue;
        ShadowViewGpu shadow = ShadowViews[light.ShadowViewOffset];
        if (shadow.MapIndex != 0) continue;
        float4 clipPosition = mul(float4(input.WorldPosition, 1.0f), shadow.LightViewProjection);
        if (clipPosition.w <= 0.0f) break;
        float3 ndc = clipPosition.xyz / clipPosition.w;
        if (any(ndc.xy < -1.0f) || any(ndc.xy > 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f) break;
        float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
        uint width, height;
        DirectionalShadowMap.GetDimensions(width, height);
        float2 filterRadiusUV;
        if (FindPcssFilterRadiusUV(uv, ndc.z, shadow, uint2(width, height), filterRadiusUV))
        {
            float2 tapRadiusUV = GetPcssTapRadiusUV(filterRadiusUV, uint2(width, height));
            radiusTexels = max(tapRadiusUV.x * width, tapRadiusUV.y * height);
        }
        break;
    }
    // Log scale: black = 0, gray levels step at 1, 3, 7, ... texels;
    // white = 1023 or more texels. No blocker is black.
    float gray = saturate(log2(1.0f + radiusTexels) / 10.0f);
    return float4(gray.xxx, GetDebugAlpha(input));
}

float4 PSDebugWorldNormal(VertexOutput input) : SV_TARGET
{
    float3 normal = GetWorldNormal(input);
    return float4(normal * 0.5f + 0.5f, GetDebugAlpha(input));
}

float4 PSDebugUV(VertexOutput input) : SV_TARGET
{
    return float4(frac(input.UV), 0.0f, GetDebugAlpha(input));
}

float4 PSDebugMipLOD(VertexOutput input) : SV_TARGET
{
    // White marks the renderer's fallback texture (no valid material texture).
    if (HasBaseColorTexture == 0) return float4(1.0f, 1.0f, 1.0f, GetDebugAlpha(input));

    uint width, height, mipCount;
    BaseColorTexture.GetDimensions(0, width, height, mipCount);
    float lod = BaseColorTexture.CalculateLevelOfDetail(LinearSampler, input.UV);
    lod = clamp(lod, 0.0f, float(max(mipCount, 1u) - 1u));

    // Distinct integer levels rotate around the color wheel; fractional LOD changes smoothly.
    float3 hue = frac(lod * 0.381966f + float3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f));
    float3 color = saturate(abs(hue * 6.0f - 3.0f) - 1.0f);
    // This reports one LOD value, not every mip touched by trilinear filtering.
    return float4(color, GetDebugAlpha(input));
}
