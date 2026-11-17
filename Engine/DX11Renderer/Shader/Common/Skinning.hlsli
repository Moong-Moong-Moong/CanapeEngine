#ifndef CANAPE_COMMON_SKINNING_HLSLI
#define CANAPE_COMMON_SKINNING_HLSLI

#include "MeshTypes.hlsli"

static const uint MAX_SKIN_MATRICES = 256;

// VS-only, 16384 bytes. b0-b8 retain their existing meanings.
// Final bind-pose-to-posed-mesh-local matrices, supplied by the CPU; World is
// applied afterwards. Indices are relative to this draw's palette, not scene IDs.
cbuffer SkinningBuffer : register(b9)
{
    row_major float4x4 SkinMatrices[MAX_SKIN_MATRICES];
};

LocalSurface ApplySkinning(LocalSurface surface, uint4 boneIndices, float4 boneWeights)
{
    // Inputs must be finite. Normalize nonnegative weights; an unweighted vertex
    // stays in mesh-local space. Only positive-weight indices must be in [0,255]
    // and refer to initialized entries. Never fetch unused palette entries.
    float4 weights = max(boneWeights, 0.0f);
    float weightSum = dot(weights, float4(1.0f, 1.0f, 1.0f, 1.0f));
    if (weightSum <= 1e-8f) return surface;
    weights /= weightSum;

    float4x4 skinMatrix = (float4x4)0;
    [unroll]
    for (uint influence = 0; influence < 4; ++influence)
    {
        if (weights[influence] > 0.0f)
            skinMatrix += SkinMatrices[boneIndices[influence]] * weights[influence];
    }

    surface.Position = mul(float4(surface.Position, 1.0f), skinMatrix).xyz;
    surface.Normal = mul(float4(surface.Normal, 0.0f), skinMatrix).xyz;
    surface.Tangent.xyz = mul(float4(surface.Tangent.xyz, 0.0f), skinMatrix).xyz;
    // UV and Tangent.w are unchanged. Normalize/rebuild the TBN in MeshSurface.
    return surface;
}

#endif
