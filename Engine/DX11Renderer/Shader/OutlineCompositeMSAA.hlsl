#include "Common/OutlineCompositeCommon.hlsli"

Texture2DMS<uint> ObjectMask : register(t0);

uint ReadMask(float2 uv, uint2 size, uint sampleIndex)
{
    if (any(uv < 0.0f) || any(uv >= 1.0f)) return 0;
    return ObjectMask.Load(int2(uv * size), sampleIndex);
}

float4 PSMain(FullscreenVertexOutput input) : SV_TARGET
{
    uint width, height, sampleCount;
    ObjectMask.GetDimensions(width, height, sampleCount);
    float3 weightedColor = 0;
    float opacitySum = 0;
    // Compare categorical IDs independently at corresponding sample locations.
    // Never resolve/average IDs: resolve opacity-weighted color and coverage.
    for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        float4 colorOpacity;
        if (FindOutline(input.UV, uint2(width, height), sampleIndex, colorOpacity))
        {
            weightedColor += colorOpacity.rgb * colorOpacity.a;
            opacitySum += colorOpacity.a;
        }
    }
    if (opacitySum <= 0) discard;
    // Straight-alpha output for the existing blend state. Different object colors
    // are weighted by their opacity before resolving sample coverage.
    return float4(weightedColor / opacitySum, opacitySum / float(sampleCount));
}
