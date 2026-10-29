Texture2D OutputTexture : register(t0);
SamplerState LinearSampler : register(s0);

cbuffer DepthDisplay : register(b0)
{
    float DepthMin;
    float DepthMax;
    float2 DepthPadding;
};

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(uint vertexID : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    // The sRGB SRV decodes to linear RGB; the back-buffer sRGB RTV encodes it once.
    return OutputTexture.Sample(LinearSampler, input.UV);
}

float4 PSCameraDepth(VertexOutput input) : SV_TARGET
{
    // D3D depth is non-linear; select a raw depth interval to reveal nearby differences.
    float depth = OutputTexture.Sample(LinearSampler, input.UV).r;
    float gray = saturate((depth - DepthMin) / (DepthMax - DepthMin));
    return float4(gray, gray, gray, 1.0f);
}
