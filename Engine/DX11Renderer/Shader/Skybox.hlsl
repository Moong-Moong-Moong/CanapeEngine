#include "Common/SkyboxEnvironment.hlsli"

struct SkyboxVertexInput
{
    float2 Position : POSITION;
};

struct SkyboxVertexOutput
{
    float4 Position : SV_POSITION;
    float3 WorldDirection : TEXCOORD0;
};

SkyboxVertexOutput VSMain(SkyboxVertexInput input)
{
    SkyboxVertexOutput output;
    float4 clipPosition = float4(input.Position, 1.0f, 1.0f);
    output.Position = clipPosition;

    float4 viewPosition = mul(clipPosition, InverseProjection);
    float3 viewDirection = viewPosition.xyz / viewPosition.w;
    // w = 0 discards camera translation; only the inverse-view rotation acts on the ray.
    float3 worldDirection = mul(float4(viewDirection, 0.0f), InverseView).xyz;
    output.WorldDirection = RotateIntoSkybox(worldDirection);
    return output;
}

float4 PSMain(SkyboxVertexOutput input) : SV_TARGET
{
    float3 direction = normalize(input.WorldDirection);
    return float4(SkyboxTexture.Sample(SkyboxSampler, direction).rgb * Brightness, 1.0f);
}
