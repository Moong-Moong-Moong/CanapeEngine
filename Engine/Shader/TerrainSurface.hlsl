#include "Common/Lighting.hlsli"

// Mirrors TerrainGeometry in Source/Simulation/TerrainGeneration.cpp.
cbuffer TerrainGeometry : register(b10)
{
    float CellSize;
    uint PointsPerSide;
    float2 pad_;
};

// Mirrors TerrainLook in the same file.
cbuffer TerrainLook : register(b11)
{
    float3 Color;
    float lookPad_;
};

// Bedrock height and cover thickness in meters at each grid point.
Texture2D<float2> Ground : register(t8);

static const uint2 CellCorners[6] = {
    uint2(0, 0),
    uint2(0, 1),
    uint2(1, 1),
    uint2(0, 0),
    uint2(1, 1),
    uint2(1, 0)
};

struct TerrainSurfaceVarying
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : WORLDPOS;
    float3 WorldNormal : NORMAL;
};

float SurfaceHeightAt(int2 gridPoint)
{
    const float2 ground = Ground.Load(int3(gridPoint, 0));
    return ground.x + ground.y;
}

float3 SurfaceNormalAt(int2 gridPoint)
{
    const int2 last = int2(PointsPerSide, PointsPerSide) - 1;
    const int2 low = max(gridPoint - 1, int2(0, 0));
    const int2 high = min(gridPoint + 1, last);
    const float2 span = float2(high - low) * CellSize;
    const float2 gradient = float2(
        SurfaceHeightAt(int2(high.x, gridPoint.y)) - SurfaceHeightAt(int2(low.x, gridPoint.y)),
        SurfaceHeightAt(int2(gridPoint.x, high.y)) - SurfaceHeightAt(int2(gridPoint.x, low.y))
    ) / span;

    return normalize(float3(-gradient.x, 1.0, -gradient.y));
}

TerrainSurfaceVarying vs_main(uint vertexId : SV_VertexID)
{
    const uint cells = PointsPerSide - 1u;
    const uint cell = vertexId / 6u;
    const int2 gridPoint = int2(CellCorners[vertexId % 6u] + uint2(cell % cells, cell / cells));

    TerrainSurfaceVarying output;
    output.WorldPosition = float3(
        float(gridPoint.x) * CellSize,
        SurfaceHeightAt(gridPoint),
        float(gridPoint.y) * CellSize
    );
    output.Position = mul(float4(output.WorldPosition, 1.0), ViewProjection);
    output.WorldNormal = SurfaceNormalAt(gridPoint);
    return output;
}

float4 ps_main(TerrainSurfaceVarying input) : SV_TARGET
{
    const float3 normal = normalize(input.WorldNormal);
    const float3 lighting = AmbientColor * AmbientIntensity
        + CalculateDirectLighting(input.WorldPosition, normal);
    return float4(Color * lighting, 1.0);
}
