cbuffer DrawConstants : register(b0)
{
    float4x4 World;
    float4x4 ViewProjection;
    float4 BaseColor;
};

Texture2D BaseColorTexture : register(t0);
SamplerState LinearSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

PSInput VS_Main(VSInput input)
{
    PSInput output;
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    output.Position = mul(worldPosition, ViewProjection);
    output.UV = input.UV;
    return output;
}

float4 PS_Main(PSInput input) : SV_TARGET
{
    return BaseColorTexture.Sample(LinearSampler, input.UV) * BaseColor;
}
