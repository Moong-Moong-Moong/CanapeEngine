#include "Common/Constants.hlsli"

Texture2D BaseColorTexture : register(t0);
SamplerState LinearSampler : register(s0);
Texture2D<float> DirectionalShadowMap : register(t1);
SamplerComparisonState ShadowSampler : register(s1);

struct VertexInput
{
    float3 Position : POSITION;
#ifdef STATIC_VERTEX
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
#endif
};

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : WORLDPOS;
    float3 WorldNormal : NORMAL;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    output.Position = mul(worldPosition, ViewProjection);
    output.WorldPosition = worldPosition.xyz;
#ifdef STATIC_VERTEX
    output.WorldNormal = mul(float4(input.Normal, 0.0f), WorldInverseTranspose).xyz;
    output.UV = input.UV;
#else
    output.WorldNormal = float3(0, 0, 0);
    output.UV = float2(0, 0);
#endif
    return output;
}

struct ShadowVertexOutput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

struct ShadowVertexInput
{
    float3 Position : POSITION;
#ifdef STATIC_VERTEX
    float2 UV : TEXCOORD0;
#endif
};

ShadowVertexOutput VSShadow(ShadowVertexInput input)
{
    ShadowVertexOutput output;
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    output.Position = mul(worldPosition, ShadowViews[0].LightViewProjection);
#ifdef STATIC_VERTEX
    output.UV = input.UV;
#else
    output.UV = 0.0f;
#endif
    return output;
}

void PSShadowMasked(ShadowVertexOutput input)
{
    // Same alpha texture, base alpha and cutoff as the main masked-material shader.
    float alpha = BaseColor.a * BaseColorTexture.Sample(LinearSampler, input.UV).a;
    clip(alpha - AlphaCutoff);
}

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
    // LESS_EQUAL implements currentDepth <= storedDepth + bias. Linear comparison
    // filtering provides hardware 2x2 PCF; point comparison is available in settings.
    return DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, uv, ndc.z - shadow.DepthBias);
}

float3 GetWorldNormal(VertexOutput input)
{
    float3 geometricNormal = cross(ddy(input.WorldPosition), ddx(input.WorldPosition));
    float3 normal = dot(input.WorldNormal, input.WorldNormal) > 1e-12f
        ? input.WorldNormal : geometricNormal;
    return normal * rsqrt(max(dot(normal, normal), 1e-20f));
}

float GetDebugAlpha(VertexOutput input)
{
    if (MaterialAlphaMode == 0) return 1.0f;
    // Preserve masked cutouts and transparent blending without using texture RGB.
    float alpha = BaseColor.a * BaseColorTexture.Sample(LinearSampler, input.UV).a;
    if (MaterialAlphaMode == 1) clip(alpha - AlphaCutoff);
    return MaterialAlphaMode == 2 ? alpha : 1.0f;
}

float3 CalculateDirectLighting(float3 worldPosition, float3 normal)
{
    float3 lighting = 0.0f;
    // A zero count executes no direct-light work; clamp to the GPU array capacities.
    [loop]
    for (uint directionalIndex = 0; directionalIndex < min(DirectionalCount, MAX_DIRECTIONAL_LIGHTS); ++directionalIndex)
    {
        DirectionalLightGpu light = DirectionalLights[directionalIndex];
        // Direction stores world travel direction, so surface-to-light is its opposite.
        float visibility = DirectionalShadowVisibility(worldPosition, light);
        lighting += light.ColorIntensity * saturate(dot(normal, -light.Direction)) * visibility;
    }
    [loop]
    for (uint localIndex = 0; localIndex < min(LocalCount, MAX_LOCAL_LIGHTS); ++localIndex)
    {
        LocalLightGpu light = LocalLights[localIndex];
        if (light.Type != LOCAL_LIGHT_POINT || light.Range <= 0.0f) continue;
        float3 toLight = light.Position - worldPosition;
        float distanceSquared = dot(toLight, toLight);
        float distanceToLight = sqrt(distanceSquared);
        if (distanceToLight >= light.Range) continue;
        // Finite at the light center and smoothly zero at the range boundary.
        float3 lightDirection = toLight * rsqrt(max(distanceSquared, 1e-12f));
        float attenuation = saturate(1.0f - distanceToLight / light.Range);
        attenuation *= attenuation;
        lighting += light.ColorIntensity * saturate(dot(normal, lightDirection)) * attenuation;
    }
    return lighting;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 normal = GetWorldNormal(input);
    float3 lighting = AmbientColor * AmbientIntensity
        + CalculateDirectLighting(input.WorldPosition, normal);
    // sRGB sampling decodes RGB; the sRGB RTV encodes the linear output once.
    float4 textureColor = BaseColorTexture.Sample(LinearSampler, input.UV);
    float alpha = BaseColor.a * textureColor.a;
    if (MaterialAlphaMode == 1) clip(alpha - AlphaCutoff);
    float3 rgb = lighting * BaseColor.rgb * textureColor.rgb;
    return float4(rgb, MaterialAlphaMode == 2 ? alpha : 1.0f);
}

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
