#include "../Common/Constants.hlsli"
#include "../Common/Material.hlsli"
#include "ShadowConstants.hlsli"
#ifdef SKINNED_VERTEX
#include "../Common/Skinning.hlsli"
#endif

struct ShadowVertexOutput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

struct ShadowVertexInput
{
    float3 Position : POSITION;
#if defined(STATIC_VERTEX) || defined(SKINNED_VERTEX)
    float2 UV : TEXCOORD0;
#endif
#ifdef SKINNED_VERTEX
    uint4 BoneIndices : BLENDINDICES0;
    float4 BoneWeights : BLENDWEIGHT0;
#endif
};

ShadowVertexOutput VSShadow(ShadowVertexInput input)
{
    ShadowVertexOutput output;
#ifdef SKINNED_VERTEX
    LocalSurface surface = (LocalSurface)0;
    surface.Position = input.Position;
    input.Position = ApplySkinning(surface, input.BoneIndices, input.BoneWeights).Position;
#endif
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    output.Position = mul(worldPosition, ShadowViews[0].LightViewProjection);
#if defined(STATIC_VERTEX) || defined(SKINNED_VERTEX)
    output.UV = input.UV;
#else
    output.UV = 0.0f;
#endif
    return output;
}

void PSShadowMasked(ShadowVertexOutput input)
{
    // Same alpha texture, base alpha and cutoff as the main masked-material shader.
    float alpha = BaseColor.a * BaseColorTexture.Sample(LinearSampler, input.UV).a;
    clip(alpha - AlphaCutoff);
}
