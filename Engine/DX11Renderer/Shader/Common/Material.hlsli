#ifndef CANAPE_COMMON_MATERIAL_HLSLI
#define CANAPE_COMMON_MATERIAL_HLSLI

#include "Constants.hlsli"
#include "MeshTypes.hlsli"

Texture2D BaseColorTexture : register(t0);
SamplerState LinearSampler : register(s0);

float GetDebugAlpha(VertexOutput input)
{
    if (MaterialAlphaMode == 0) return 1.0f;
    // Preserve masked cutouts and transparent blending without using texture RGB.
    float alpha = BaseColor.a * BaseColorTexture.Sample(LinearSampler, input.UV).a;
    if (MaterialAlphaMode == 1) clip(alpha - AlphaCutoff);
    return MaterialAlphaMode == 2 ? alpha : 1.0f;
}

#endif
