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
    float3 lightDirection = normalize(float3(0.4f, 1.0f, -0.6f));
    float diffuse = abs(dot(normal, lightDirection)) * 0.75f + 0.25f;
    // sRGB sampling decodes RGB; the sRGB RTV encodes the linear output once.
    float4 textureColor = BaseColorTexture.Sample(LinearSampler, input.UV);
    float alpha = BaseColor.a * textureColor.a;
    if (MaterialAlphaMode == 1) clip(alpha - AlphaCutoff);
    float3 rgb = diffuse.xxx * float3(0.85f, 0.85f, 0.9f) * BaseColor.rgb * textureColor.rgb;
    return float4(rgb, MaterialAlphaMode == 2 ? alpha : 1.0f);
}
