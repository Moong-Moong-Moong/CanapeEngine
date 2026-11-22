#ifndef CANAPE_OUTLINE_COMPOSITE_COMMON_HLSLI
#define CANAPE_OUTLINE_COMPOSITE_COMMON_HLSLI

#include "FullscreenTypes.hlsli"

cbuffer OutlineCompositeConstants : register(b11)
{
    uint StyleCount;
    uint MaxThickness;
    float2 OutputSize;
};

// 32-byte elements; matches Data/OutlineStyle.h.
struct OutlineStyle
{
    float3 Color;
    uint Thickness;
    float Opacity;
    float3 Padding;
};
StructuredBuffer<OutlineStyle> OutlineStyles : register(t9);

// Each entry file supplies the read operation for its own texture type.
uint ReadMask(float2 uv, uint2 size, uint sampleIndex);

bool FindOutline(float2 uv, uint2 size, uint sampleIndex, out float4 colorOpacity)
{
    colorOpacity = 0;
    uint center = ReadMask(uv, size, sampleIndex);
    int radius = int(MaxThickness);
    uint bestDistance = 0xFFFFFFFFu;
    uint bestId = 0xFFFFFFFFu;
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            uint distanceSquared = uint(x * x + y * y);
            if (distanceSquared > uint(radius * radius) || distanceSquared > bestDistance) continue;
            uint neighbor = ReadMask(uv + float2(x, y) / OutputSize, size, sampleIndex);
            if (neighbor == 0 || neighbor == center || neighbor >= StyleCount) continue;
            OutlineStyle style = OutlineStyles[neighbor];
            if (style.Opacity <= 0 || distanceSquared > style.Thickness * style.Thickness) continue;
            // Nearest eligible boundary wins; equal distances use the lower mask ID.
            // The neighbor owns the style, including when the center is background.
            if (distanceSquared < bestDistance || neighbor < bestId)
            {
                bestDistance = distanceSquared;
                bestId = neighbor;
                colorOpacity = float4(style.Color, style.Opacity);
            }
        }
    }
    return bestId != 0xFFFFFFFFu;
}

#endif
