Texture2D<float> ShadowDepth : register(t0);
SamplerState DebugSampler : register(s0);

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1) & 2, vertexId & 2);
    output.Position = float4(output.UV.x * 2.0f - 1.0f,
        1.0f - output.UV.y * 2.0f, 0.0f, 1.0f);
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float depth = ShadowDepth.SampleLevel(DebugSampler, input.UV, 0).r;
    return float4(depth.xxx, 1.0f);
}
