cbuffer ShadowObjectBuffer : register(b0)
{
    float4x4 WorldLightViewProjection;
};

float4 VSMain(float3 position : POSITION) : SV_POSITION
{
    return mul(float4(position, 1.0f), WorldLightViewProjection);
}
