#include "../Common/FullscreenTypes.hlsli"

Texture2D OutputTexture : register(t0);
SamplerState LinearSampler : register(s0);

cbuffer DepthDisplay : register(b0)
{
    float DepthMin;
    float DepthMax;
    float2 DepthPadding;
};

float4 PSCameraDepth(FullscreenVertexOutput input) : SV_TARGET
{
    // D3D depth is non-linear; select a raw depth interval to reveal nearby differences.
    float depth = OutputTexture.Sample(LinearSampler, input.UV).r;
    float gray = saturate((depth - DepthMin) / (DepthMax - DepthMin));
    return float4(gray, gray, gray, 1.0f);
}
