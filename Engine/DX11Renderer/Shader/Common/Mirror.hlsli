#ifndef CANAPE_COMMON_MIRROR_HLSLI
#define CANAPE_COMMON_MIRROR_HLSLI

Texture2D<float4> MirrorTexture : register(t11);

// position is the pixel's SV_POSITION, ndcOffset is in the main view (x right, y up).
// The mirror view is flipped in x, and outside the texture the lookup folds back once at the border.
float3 MirrorColor(float4 position, float2 ndcOffset)
{
    uint width, height;
    MirrorTexture.GetDimensions(width, height);
    const float2 size = float2(width, height);
    const float2 last = size - 1.0;
    const float2 pixel = position.xy + ndcOffset * float2(0.5, -0.5) * size;
    const float2 flipped = float2(size.x - pixel.x, pixel.y) - 0.5;
    const float2 texel = max(last - abs(last - abs(flipped)), 0.0);
    const int2 corner = int2(texel);
    const float2 weight = frac(texel);

    const float3 top = lerp(
        MirrorTexture.Load(int3(corner, 0)).rgb,
        MirrorTexture.Load(int3(corner, 0), int2(1, 0)).rgb,
        weight.x
    );
    const float3 bottom = lerp(
        MirrorTexture.Load(int3(corner, 0), int2(0, 1)).rgb,
        MirrorTexture.Load(int3(corner, 0), int2(1, 1)).rgb,
        weight.x
    );
    return lerp(top, bottom, weight.y);
}

#endif
