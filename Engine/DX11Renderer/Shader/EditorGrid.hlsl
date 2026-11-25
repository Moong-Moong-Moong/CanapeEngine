#include "Common/Constants.hlsli"

cbuffer EditorGridConstants : register(b12)
{
    float GridCellSize;
    float GridMajorEvery;
    float GridExtent;
    uint GridPlane;
};

static const uint GRID_PLANE_XZ = 0;
static const uint GRID_PLANE_XY = 1;
static const uint GRID_PLANE_ZY = 2;

struct GridVertexOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
};

static const float2 GridCorners[6] =
{
    float2(-1.0f, -1.0f), float2(-1.0f, 1.0f), float2(1.0f, 1.0f),
    float2(-1.0f, -1.0f), float2(1.0f, 1.0f), float2(1.0f, -1.0f)
};

static const float3 MinorColor = float3(0.45f, 0.45f, 0.45f);
static const float3 MajorColor = float3(0.65f, 0.65f, 0.65f);
static const float3 AxisXColor = float3(0.9f, 0.15f, 0.15f);
static const float3 AxisYColor = float3(0.2f, 0.8f, 0.2f);
static const float3 AxisZColor = float3(0.15f, 0.35f, 0.95f);

float2 WorldToPlane(float3 world)
{
    if (GridPlane == GRID_PLANE_XY) return world.xy;
    if (GridPlane == GRID_PLANE_ZY) return world.zy;
    return world.xz;
}

float3 PlaneToWorld(float2 planar)
{
    if (GridPlane == GRID_PLANE_XY) return float3(planar.x, planar.y, 0.0f);
    if (GridPlane == GRID_PLANE_ZY) return float3(0.0f, planar.y, planar.x);
    return float3(planar.x, 0.0f, planar.y);
}

float3 FirstAxisColor()
{
    return GridPlane == GRID_PLANE_ZY ? AxisZColor : AxisXColor;
}

float3 SecondAxisColor()
{
    return GridPlane == GRID_PLANE_XZ ? AxisZColor : AxisYColor;
}

GridVertexOutput VSMain(uint vertexId : SV_VertexID)
{
    float2 planar = WorldToPlane(CameraPosition) + GridCorners[vertexId] * GridExtent;
    float3 world = PlaneToWorld(planar);

    GridVertexOutput output;
    output.Position = mul(float4(world, 1.0f), ViewProjection);
    output.WorldPosition = world;
    return output;
}

float LineCoverage(float2 coord, float pixelWidth)
{
    float2 derivative = max(fwidth(coord), 1e-5f);
    float2 distanceInPixels = abs(frac(coord - 0.5f) - 0.5f) / derivative;
    float2 coverage = 1.0f - saturate(distanceInPixels / pixelWidth);
    float2 density = saturate(1.5f - derivative * 6.0f);
    coverage *= density;
    return max(coverage.x, coverage.y);
}

float AxisCoverage(float value, float pixelWidth)
{
    float derivative = max(fwidth(value), 1e-5f);
    return 1.0f - saturate(abs(value) / (derivative * pixelWidth));
}

float4 PSMain(GridVertexOutput input) : SV_TARGET
{
    float2 planar = WorldToPlane(input.WorldPosition);
    float2 cell = planar / GridCellSize;

    float minor = LineCoverage(cell, 1.0f);
    float major = LineCoverage(cell / GridMajorEvery, 1.5f);

    float3 color = MinorColor;
    float alpha = minor * 0.3f;
    if (major * 0.6f > alpha)
    {
        color = MajorColor;
        alpha = major * 0.6f;
    }

    float firstAxis = AxisCoverage(planar.y, 2.0f);
    float secondAxis = AxisCoverage(planar.x, 2.0f);
    color = lerp(color, FirstAxisColor(), firstAxis);
    alpha = max(alpha, firstAxis * 0.9f);
    color = lerp(color, SecondAxisColor(), secondAxis);
    alpha = max(alpha, secondAxis * 0.9f);

    float distanceFade = saturate(1.0f - length(planar - WorldToPlane(CameraPosition)) / GridExtent);
    alpha *= distanceFade * distanceFade;

    clip(alpha - 0.001f);
    return float4(color, alpha);
}
