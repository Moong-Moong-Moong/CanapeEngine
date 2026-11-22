#include "Common/OutlineCompositeCommon.hlsli"

Texture2D<uint> ObjectMask : register(t0);

uint ReadMask(float2 uv, uint2 size, uint sampleIndex)
{
    if (any(uv < 0.0f) || any(uv >= 1.0f)) return 0;
    return ObjectMask.Load(int3(int2(uv * size), 0));
}

float4 PSMain(FullscreenVertexOutput input) : SV_TARGET
{
    uint width, height;
    ObjectMask.GetDimensions(width, height);
    float4 colorOpacity;
    if (!FindOutline(input.UV, uint2(width, height), 0, colorOpacity)) discard;
    return colorOpacity;
}
