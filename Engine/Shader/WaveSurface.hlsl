#include "Common/Lighting.hlsli"
#include "Common/Mirror.hlsli"
#include "Simulation.hlsli"
#include "Swell.hlsli"

// Mirrors WaveSurfaceGeometry in Source/Simulation/WaveSurfaces.cpp.
cbuffer WaveSurfaceGeometry : register(b10)
{
    float2 AreaOrigin;
    float2 AreaExtent;
    float BaseHeight;
    uint PointsPerSide;
    float2 pad_;
};

// Mirrors WaveSurfaceLook in the same file.
cbuffer WaveSurfaceLook : register(b11)
{
    float3 DeepColor;
    float pad0_;
    float3 ShallowColor;
    float pad1_;
};

// Ripple height and vertical velocity at each grid point.
Texture2D<float2> Ripples : register(t8);

static const uint2 CellCorners[6] = {
    uint2(0, 0),
    uint2(0, 1),
    uint2(1, 1),
    uint2(0, 0),
    uint2(1, 1),
    uint2(1, 0)
};
static const float GlintPower = 200.0;
// The mirror texture holds no distance, so what the sea reflects is taken to be this far away.
static const float MirrorDistanceMeters = 1.5;

struct WaveSurfaceVarying
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : WORLDPOS;
    float3 WorldNormal : NORMAL;
};

float2 CellSize()
{
    return AreaExtent / float(PointsPerSide - 1u);
}

// The grid cannot carry a shorter wave.
float ShortestWavelength()
{
    const float2 cellSize = CellSize();
    return 2.0 * max(cellSize.x, cellSize.y);
}

float RippleAt(int2 gridPoint)
{
    const int2 last = int2(PointsPerSide, PointsPerSide) - 1;
    return Ripples.Load(int3(clamp(gridPoint, int2(0, 0), last), 0)).x;
}

float RippleHeight(int2 gridPoint, out float2 gradient)
{
    gradient = float2(
        RippleAt(gridPoint + int2(1, 0)) - RippleAt(gridPoint - int2(1, 0)),
        RippleAt(gridPoint + int2(0, 1)) - RippleAt(gridPoint - int2(0, 1))
    ) / (2.0 * CellSize());

    return RippleAt(gridPoint);
}

WaveSurfaceVarying vs_main(uint vertexId : SV_VertexID)
{
    const uint cells = PointsPerSide - 1u;
    const uint cell = vertexId / 6u;
    const uint2 gridPoint = CellCorners[vertexId % 6u] + uint2(cell % cells, cell / cells);
    const float2 xz = AreaOrigin + float2(gridPoint) / float(cells) * AreaExtent;

    float2 gradient;
    float2 rippleGradient;
    const float height = BaseHeight
        + SwellHeight(xz, ShortestWavelength(), gradient)
        + RippleHeight(int2(gridPoint), rippleGradient);
    gradient += rippleGradient;

    WaveSurfaceVarying output;
    output.WorldPosition = float3(xz.x, height, xz.y);
    output.Position = mul(float4(output.WorldPosition, 1.0), ViewProjection);
    output.WorldNormal = normalize(float3(-gradient.x, 1.0, -gradient.y));
    return output;
}

float2 NdcPosition(float3 worldPosition)
{
    const float4 position = mul(float4(worldPosition, 1.0), ViewProjection);
    return position.xy / position.w;
}

float2 MirrorNdcOffset(float3 worldPosition, float3 toCamera, float3 normal)
{
    float3 reflected = reflect(-toCamera, normal);
    reflected.y = max(reflected.y, 0.0);

    const float3 hit = worldPosition + reflected * MirrorDistanceMeters;
    const float3 hitImage = float3(hit.x, 2.0 * BaseHeight - hit.y, hit.z);
    return NdcPosition(hitImage) - NdcPosition(worldPosition);
}

float4 ps_main(WaveSurfaceVarying input) : SV_TARGET
{
    const float3 normal = normalize(input.WorldNormal);
    const float3 toCamera = normalize(CameraPosition - input.WorldPosition);
    const float grazing = 1.0 - saturate(dot(normal, toCamera));
    const float fresnel = 0.02 + 0.98 * pow(grazing, 5.0);
    const float3 albedo = lerp(DeepColor, ShallowColor, fresnel);

    float3 glint = 0.0;
    [loop]
    for (uint i = 0; i < min(DirectionalCount, MAX_DIRECTIONAL_LIGHTS); ++i)
    {
        const DirectionalLightGpu light = DirectionalLights[i];
        // Direction stores world travel direction, so surface-to-light is its opposite.
        const float3 halfway = normalize(toCamera - light.Direction);
        glint += light.ColorIntensity * pow(saturate(dot(normal, halfway)), GlintPower) * DirectionalShadowVisibility(input.WorldPosition, light);
    }
    glint *= fresnel * (GlintPower + 8.0) / (4.0 * TwoPi);

    const float3 lighting = AmbientColor * AmbientIntensity
        + CalculateDirectLighting(input.WorldPosition, normal);
    const float2 mirrorOffset = MirrorNdcOffset(input.WorldPosition, toCamera, normal);
    // A lerp of the water color and the mirror by fresnel would be physically closer;
    // the sum keeps the waves visible.
    return float4(albedo * lighting + glint + fresnel * MirrorColor(input.Position, mirrorOffset), 1.0);
}
