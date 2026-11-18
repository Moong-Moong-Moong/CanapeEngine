#include "Common/FullscreenTypes.hlsli"
#include "Common/IBLSampling.hlsli"

TextureCube EnvironmentMap : register(t0);
SamplerState EnvironmentSampler : register(s0);

cbuffer IrradianceFace : register(b0)
{
    uint FaceIndex;
    uint SampleCount;
    uint2 Padding;
};

float3 CosineHemisphere(float2 xi)
{
    float radius = sqrt(xi.x);
    float angle = 2.0f * IBL_PI * xi.y;
    return float3(radius * cos(angle), radius * sin(angle), sqrt(1.0f - xi.x));
}

float4 PSIrradiance(FullscreenVertexOutput input) : SV_TARGET
{
    float3 n = IBLCubeDirection(FaceIndex, input.UV);
    float3 tangent, bitangent;
    IBLTangentBasis(n, tangent, bitangent);
    float3 sum = 0.0f;
    [loop]
    for (uint i = 0; i < SampleCount; ++i)
    {
        float3 h = CosineHemisphere(IBLHammersley(i, SampleCount));
        float3 l = tangent * h.x + bitangent * h.y + n * h.z;
        sum += EnvironmentMap.SampleLevel(EnvironmentSampler, l, 0.0f).rgb;
    }
    // PDF = NdotL / PI. For E/PI, the Monte Carlo weight (NdotL / PI) / PDF is 1.
    return float4(sum / float(SampleCount), 1.0f);
}
