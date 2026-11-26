#ifndef CANAPE_COMMON_MESHTYPES_HLSLI
#define CANAPE_COMMON_MESHTYPES_HLSLI

struct PositionVertexInput
{
    float3 Position : POSITION;
};

struct StaticVertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    float4 Tangent : TANGENT;
    float2 UV1 : TEXCOORD1;
};

struct SkinnedVertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    float4 Tangent : TANGENT;
    uint4 BoneIndices : BLENDINDICES0;
    float4 BoneWeights : BLENDWEIGHT0;
    float2 UV1 : TEXCOORD1;
};

// Mesh-local data, before World is applied. No pipeline semantics here.
struct LocalSurface
{
    float3 Position;
    float3 Normal;
    float2 UV;
    float4 Tangent;
    float2 UV1;
};

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : WORLDPOS;
    float3 WorldNormal : NORMAL;
    float2 UV : TEXCOORD0;
    float4 Tangent : TANGENT;
    float2 UV1 : TEXCOORD1;
};

#endif
