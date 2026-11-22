#include "Common/Material.hlsli"

cbuffer OutlineMaskConstants : register(b11)
{
    uint MaskId;
    uint3 MaskPadding;
};

uint PSMain(VertexOutput input) : SV_TARGET
{
    // Identical texture, sampler, UVs, base alpha and cutoff to MeshPS.
    if (MaterialAlphaMode == 1)
        clip(BaseColor.a * BaseColorTexture.Sample(LinearSampler, input.UV).a - AlphaCutoff);
    return MaskId;
}
