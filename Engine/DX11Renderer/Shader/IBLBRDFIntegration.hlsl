#include "Common/FullscreenTypes.hlsli"
#include "Common/IBLSampling.hlsli"

cbuffer BRDFIntegration : register(b0)
{
    uint SampleCount;
    uint3 Padding;
};

float GeometrySchlickGGXIBL(float nDotX, float roughness)
{
    // IBL uses k = alpha / 2; direct lighting's (roughness + 1)^2 / 8 is a
    // separate approximation for punctual lights.
    float alpha = roughness * roughness;
    float k = alpha * 0.5f;
    return nDotX / max(nDotX * (1.0f - k) + k, 1e-6f);
}

float2 PSBRDFIntegration(FullscreenVertexOutput input) : SV_TARGET
{
    // Fullscreen UV is evaluated at texel centers, so neither axis reaches exact zero.
    float nDotV = saturate(input.UV.x);
    float roughness = saturate(input.UV.y);
    float3 n = float3(0.0f, 0.0f, 1.0f);
    float3 v = float3(sqrt(max(0.0f, 1.0f - nDotV * nDotV)), 0.0f, nDotV);
    float a = 0.0f;
    float b = 0.0f;
    [loop]
    for (uint i = 0; i < SampleCount; ++i)
    {
        float3 h = IBLImportanceSampleGGX(IBLHammersley(i, SampleCount), n, roughness);
        float vDotH = saturate(dot(v, h));
        float3 l = normalize(2.0f * vDotH * h - v);
        float nDotL = saturate(l.z);
        float nDotH = saturate(h.z);
        if (nDotL <= 0.0f || nDotH <= 0.0f || vDotH <= 0.0f) continue;

        float geometry = GeometrySchlickGGXIBL(nDotV, roughness)
            * GeometrySchlickGGXIBL(nDotL, roughness);
        // Sampling H with PDF D*NdotH gives PDF_L = D*NdotH/(4*VdotH).
        // This cancels D and the NdotL factor in the BRDF integral.
        float visibility = geometry * vDotH / max(nDotH * nDotV, 1e-6f);
        float fc = pow(1.0f - vDotH, 5.0f);
        a += (1.0f - fc) * visibility;
        b += fc * visibility;
    }
    return float2(a, b) / float(SampleCount);
}
