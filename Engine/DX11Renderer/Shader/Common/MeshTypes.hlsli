#ifndef CANAPE_COMMON_MESHTYPES_HLSLI
#define CANAPE_COMMON_MESHTYPES_HLSLI

struct VertexInput
{
    float3 Position : POSITION;
#ifdef STATIC_VERTEX
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    float4 Tangent : TANGENT;
#endif
};

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : WORLDPOS;
    float3 WorldNormal : NORMAL;
    float2 UV : TEXCOORD0;
    float4 Tangent : TANGENT;
};

#endif
