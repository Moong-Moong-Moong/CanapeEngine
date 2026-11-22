#ifndef CANAPE_COMMON_MIRROR_HLSLI
#define CANAPE_COMMON_MIRROR_HLSLI

Texture2D<float4> MirrorTexture : register(t11);

// The mirror view is flipped in x, so the texel is read from the opposite column.
float3 MirrorColor(float4 position)
{
    uint width, height;
    MirrorTexture.GetDimensions(width, height);
    return MirrorTexture.Load(int3(int(width) - 1 - int(position.x), int(position.y), 0)).rgb;
}

#endif
