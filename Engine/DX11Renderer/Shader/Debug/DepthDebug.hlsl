#include "../Common/FullscreenTypes.hlsli"

#if defined(MSAA_DEPTH)
Texture2DMS<float> OutputTexture : register(t0);
#else
Texture2D OutputTexture : register(t0);
SamplerState LinearSampler : register(s0);
#endif

cbuffer DepthDisplay : register(b0)
{
    float DepthMin;
    float DepthMax;
    float2 DepthPadding;
};

float4 PSCameraDepth(FullscreenVertexOutput input) : SV_TARGET
{
    // D3D depth is non-linear; select a raw depth interval to reveal nearby differences.
#if defined(MSAA_DEPTH)
    uint width, height, sampleCount;
    OutputTexture.GetDimensions(width, height, sampleCount);
    // Map output UVs to source texels even when the output has a different resolution.
    int2 pixelCoord = int2(saturate(input.UV) * float2(width, height));
    pixelCoord = min(pixelCoord, int2(width, height) - 1);
    float depth = OutputTexture.Load(pixelCoord, 0);
#else
    float depth = OutputTexture.Sample(LinearSampler, input.UV).r;
#endif
    float gray = saturate((depth - DepthMin) / (DepthMax - DepthMin));
    return float4(gray, gray, gray, 1.0f);
}
