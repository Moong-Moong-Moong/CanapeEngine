#include "../Common/Constants.hlsli"
#include "../Common/Material.hlsli"
#include "ShadowConstants.hlsli"

struct ShadowVertexOutput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

struct ShadowVertexInput
{
    float3 Position : POSITION;
#ifdef STATIC_VERTEX
    float2 UV : TEXCOORD0;
#endif
};

ShadowVertexOutput VSShadow(ShadowVertexInput input)
{
    ShadowVertexOutput output;
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    output.Position = mul(worldPosition, ShadowViews[0].LightViewProjection);
#ifdef STATIC_VERTEX
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
