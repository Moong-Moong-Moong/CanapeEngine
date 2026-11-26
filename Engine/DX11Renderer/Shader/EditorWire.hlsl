#include "Common/Constants.hlsli"

// Matches ShaderConstants::EditorWireConstants. b12 is private to this pass.
cbuffer EditorWireBuffer : register(b12)
{
    float3 WireCenter;
    float WireRadius;
    float4 WireColor;
};

// Keep in sync with ShaderConstants::EditorWireSegments.
static const uint SEGMENTS = 64;

float4 VSMain(uint vertexId : SV_VertexID) : SV_POSITION
{
    uint circle = vertexId / (SEGMENTS * 2);
    uint edgeVertex = vertexId % (SEGMENTS * 2);
    uint pointIndex = edgeVertex / 2 + edgeVertex % 2;
    float angle = 6.28318530718f * float(pointIndex % SEGMENTS) / float(SEGMENTS);
    float sine, cosine;
    sincos(angle, sine, cosine);
    float3 offset = circle == 0 ? float3(cosine, sine, 0.0f)
        : circle == 1 ? float3(cosine, 0.0f, sine) : float3(0.0f, cosine, sine);
    return mul(float4(WireCenter + WireRadius * offset, 1.0f), ViewProjection);
}

float4 PSMain() : SV_TARGET
{
    return WireColor;
}
