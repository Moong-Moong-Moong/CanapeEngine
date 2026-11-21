#include "Common/FullscreenTypes.hlsli"
#include "Common/IBLSampling.hlsli"

TextureCube EnvironmentMap : register(t0);
SamplerState EnvironmentSampler : register(s0);

cbuffer PrefilterFace : register(b0)
{
    uint FaceIndex;
    uint SampleCount;
    float Roughness;
    float SourceResolution;
    uint SourceMipCount;
    uint3 Padding;
};

float4 PSPrefilter(FullscreenVertexOutput input) : SV_TARGET
{
    float3 n = IBLCubeDirection(FaceIndex, input.UV);
    float3 v = n;
    if (Roughness <= 0.0f)
        return float4(EnvironmentMap.SampleLevel(EnvironmentSampler, n, 0.0f).rgb, 1.0f);

    float3 radiance = 0.0f;
    float totalWeight = 0.0f;
    float alpha = Roughness * Roughness;
    float alphaSquared = alpha * alpha;
    [loop]
    for (uint i = 0; i < SampleCount; ++i)
    {
        float3 h = IBLImportanceSampleGGX(IBLHammersley(i, SampleCount), n, Roughness);
        float vDotH = saturate(dot(v, h));
        float3 l = normalize(2.0f * vDotH * h - v);
        float nDotL = saturate(dot(n, l));
        if (nDotL <= 0.0f || vDotH <= 0.0f) continue;

        float nDotH = saturate(dot(n, h));
        float term = nDotH * nDotH * (alphaSquared - 1.0f) + 1.0f;
        float distribution = alphaSquared / max(IBL_PI * term * term, 1e-7f);
        float pdf = max(distribution * nDotH / (4.0f * vDotH), 1e-6f);
        float sourceLod = IBLSourceMipFromPdf(pdf, SampleCount, SourceResolution, SourceMipCount);
        radiance += EnvironmentMap.SampleLevel(EnvironmentSampler, l, sourceLod).rgb * nDotL;
        totalWeight += nDotL;
    }
    // Environment radiance prefilter only; Fresnel and Geometry belong to the BRDF LUT.
    return float4(totalWeight > 0.0f ? radiance / totalWeight
        : EnvironmentMap.SampleLevel(EnvironmentSampler, n, 0.0f).rgb, 1.0f);
}
