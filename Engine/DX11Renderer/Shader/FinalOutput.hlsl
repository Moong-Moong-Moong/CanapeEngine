#include "Common/FullscreenTypes.hlsli"

Texture2D OutputTexture : register(t0);
SamplerState LinearSampler : register(s0);

FullscreenVertexOutput VSMain(uint vertexID : SV_VertexID)
{
    FullscreenVertexOutput output;
    output.UV = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float4 PSMain(FullscreenVertexOutput input) : SV_TARGET
{
    // The sRGB SRV decodes to linear RGB; the back-buffer sRGB RTV encodes it once.
    return OutputTexture.Sample(LinearSampler, input.UV);
}
