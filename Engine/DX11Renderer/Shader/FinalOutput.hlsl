#include "Common/FullscreenTypes.hlsli"

Texture2D OutputTexture : register(t0);
SamplerState LinearSampler : register(s0);

// Keep the 16-byte layout in sync with ShaderConstants::PostProcessConstants.
// b7 belongs to PCSS; final composition uses b8.
cbuffer PostProcessConstants : register(b8)
{
    uint PostProcessingEnabled;
    uint ToneMappingEnabled;
    uint DebugViewEnabled;
    uint PostProcessPadding;
};

float3 ToneMapReinhard(float3 color)
{
    return color / (1.0f + color);
}

FullscreenVertexOutput VSMain(uint vertexID : SV_VertexID)
{
    FullscreenVertexOutput output;
    output.UV = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float4 PSMain(FullscreenVertexOutput input) : SV_TARGET
{
    float4 sceneColor = OutputTexture.Sample(LinearSampler, input.UV);
    float3 color = sceneColor.rgb; // FP16 SceneColor is already linear.
    if (PostProcessingEnabled != 0 && ToneMappingEnabled != 0 && DebugViewEnabled == 0)
        color = ToneMapReinhard(max(color, 0.0f));
    // Clamp only at the SDR boundary, including when post processing is disabled.
    // The destination sRGB RTV performs the sole linear-to-sRGB conversion.
    return float4(saturate(color), sceneColor.a);
}
