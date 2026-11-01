// The renderer uploads row-major matrices without transposing them.
// A future skybox pass supplies both inverses once per camera update.
cbuffer SkyboxViewBuffer : register(b6)
{
    row_major float4x4 InverseProjection;
    row_major float4x4 InverseView;
};

TextureCube SkyboxTexture : register(t2);
SamplerState SkyboxSampler : register(s2);

struct SkyboxVertexInput
{
    float2 Position : POSITION;
};

struct SkyboxVertexOutput
{
    float4 Position : SV_POSITION;
    float3 WorldDirection : TEXCOORD0;
};

SkyboxVertexOutput VSMain(SkyboxVertexInput input)
{
    SkyboxVertexOutput output;
    float4 clipPosition = float4(input.Position, 1.0f, 1.0f);
    output.Position = clipPosition;

    float4 viewPosition = mul(clipPosition, InverseProjection);
    float3 viewDirection = viewPosition.xyz / viewPosition.w;
    // w = 0 discards camera translation; only the inverse-view rotation acts on the ray.
    output.WorldDirection = mul(float4(viewDirection, 0.0f), InverseView).xyz;
    return output;
}

float4 PSMain(SkyboxVertexOutput input) : SV_TARGET
{
    float3 direction = normalize(input.WorldDirection);
    return float4(SkyboxTexture.Sample(SkyboxSampler, direction).rgb, 1.0f);
}
