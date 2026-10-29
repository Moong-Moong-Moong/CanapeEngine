#include "Common/Constants.hlsli"

Texture2D BaseColorTexture : register(t0);
SamplerState LinearSampler : register(s0);

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

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 normal = GetWorldNormal(input);
    float3 lightDirection = normalize(float3(0.4f, 1.0f, -0.6f));
    float diffuse = abs(dot(normal, lightDirection)) * 0.75f + 0.25f;
    // sRGB sampling decodes RGB; the sRGB RTV encodes the linear output once.
    float4 textureColor = BaseColorTexture.Sample(LinearSampler, input.UV);
    float alpha = BaseColor.a * textureColor.a;
    if (MaterialAlphaMode == 1) clip(alpha - AlphaCutoff);
    float3 rgb = diffuse.xxx * float3(0.85f, 0.85f, 0.9f) * BaseColor.rgb * textureColor.rgb;
    return float4(rgb, MaterialAlphaMode == 2 ? alpha : 1.0f);
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
