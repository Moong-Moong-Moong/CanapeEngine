#ifndef CANAPE_CONSTANTS_HLSLI
#define CANAPE_CONSTANTS_HLSLI

// Row-major packing is selected by the compiler. CPU uploads are not transposed.
cbuffer ObjectBuffer : register(b0)
{
    float4x4 WorldViewProjection;
    float4x4 World;
    float4x4 WorldNormal;
};

// Linear RGB and alpha. Layout matches the 32-byte CPU MaterialConstants.
cbuffer MaterialBuffer : register(b1)
{
    float4 BaseColor;
    uint MaterialAlphaMode;
    float AlphaCutoff;
    float2 MaterialPadding;
};

#endif
