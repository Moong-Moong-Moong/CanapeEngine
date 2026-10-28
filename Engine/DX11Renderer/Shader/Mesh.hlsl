#include "Common/Constants.hlsli"

Texture2D BaseColorTexture : register(t0);
SamplerState LinearSampler : register(s0);
Texture2D<float> ShadowMap : register(t1);
SamplerState ShadowSampler : register(s1);

// Matches the CPU ShadowFrameConstants layout; row-major packing is selected by the compiler.
cbuffer ShadowFrameBuffer : register(b2)
{
    float4x4 LightViewProjection;
    float3 LightDirection;
    float ShadowDepthBias;
    uint ShadowEnabled;
    uint ShowShadowFactor;
    float2 ShadowPadding;
};

float ComputeShadowFactor(float3 worldPosition)
{
    if (ShadowEnabled == 0) return 1.0f;

    float4 lightClip = mul(float4(worldPosition, 1.0f), LightViewProjection);
    if (lightClip.w <= 0.0f) return 1.0f;
    float3 ndc = lightClip.xyz / lightClip.w;
    if (any(ndc.xy < -1.0f) || any(ndc.xy > 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f)
        return 1.0f;

    // D3D NDC is +Y up and the texture origin is at the top left; Z is already [0, 1].
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float storedDepth = ShadowMap.SampleLevel(ShadowSampler, uv, 0);
    return ndc.z - ShadowDepthBias <= storedDepth ? 1.0f : 0.0f;
}

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
    output.Position = mul(float4(input.Position, 1.0f), WorldViewProjection);
    output.WorldPosition = mul(float4(input.Position, 1.0f), World).xyz;
#ifdef STATIC_VERTEX
    output.WorldNormal = mul(float4(input.Normal, 0.0f), WorldNormal).xyz;
    output.UV = input.UV;
#else
    output.WorldNormal = float3(0, 0, 0);
    output.UV = float2(0, 0);
#endif
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 geometricNormal = cross(ddy(input.WorldPosition), ddx(input.WorldPosition));
    float3 normal = dot(input.WorldNormal, input.WorldNormal) > 1e-12f
        ? input.WorldNormal : geometricNormal;
    normal *= rsqrt(max(dot(normal, normal), 1e-20f));
    float directDiffuse = abs(dot(normal, LightDirection)) * 0.75f;
    // sRGB sampling decodes RGB; the sRGB RTV encodes the linear output once.
    float4 textureColor = BaseColorTexture.Sample(LinearSampler, input.UV);
    float alpha = BaseColor.a * textureColor.a;
    if (MaterialAlphaMode == 1) clip(alpha - AlphaCutoff);
    float shadowFactor = ComputeShadowFactor(input.WorldPosition);
    if (ShowShadowFactor != 0)
        return float4(shadowFactor.xxx, MaterialAlphaMode == 2 ? alpha : 1.0f);
    float diffuse = 0.25f + directDiffuse * shadowFactor;
    float3 rgb = diffuse.xxx * float3(0.85f, 0.85f, 0.9f) * BaseColor.rgb * textureColor.rgb;
    return float4(rgb, MaterialAlphaMode == 2 ? alpha : 1.0f);
}
