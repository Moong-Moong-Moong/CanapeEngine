#include "Common/Lighting.hlsli"
#include "Simulation.hlsli"

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

// Relative to the main wave; Turn in radians.
struct SwellWave
{
    float WavelengthScale;
    float AmplitudeScale;
    float Turn;
};

static const float TwoPi = 6.28318530718;
static const float Gravity = 9.81;
// Of the main wave, over WindSpeed^2 / Gravity: half the height of a fully developed sea.
static const float SwellAmplitudeFactor = 0.105;
static const uint SwellWaveCount = 3;
static const SwellWave SwellWaves[SwellWaveCount] = {
    { 1.0, 1.0, 0.0 },
    { 0.53, 0.45, 0.6 },
    { 0.29, 0.2, -0.9 }
};
static const uint2 CellCorners[6] = {
    uint2(0, 0),
    uint2(0, 1),
    uint2(1, 1),
    uint2(0, 0),
    uint2(1, 1),
    uint2(1, 0)
};
static const float GlintPower = 200.0;

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

float SwellHeight(float2 xz, out float2 gradient)
{
    // Deep water: a wave as fast as the wind is this long.
    const float mainWavelength = TwoPi * WindSpeed * WindSpeed / Gravity;
    const float mainAmplitude = SwellAmplitudeFactor * WindSpeed * WindSpeed / Gravity;
    const float2 cellSize = CellSize();
    // The grid cannot carry a shorter wave.
    const float shortestWavelength = 2.0 * max(cellSize.x, cellSize.y);

    float height = 0.0;
    gradient = float2(0.0, 0.0);

    [unroll]
    for (uint i = 0; i < SwellWaveCount; ++i)
    {
        const SwellWave wave = SwellWaves[i];
        float sinTurn;
        float cosTurn;
        sincos(wave.Turn, sinTurn, cosTurn);
        const float2 direction = float2(
            WindDirection.x * cosTurn - WindDirection.y * sinTurn,
            WindDirection.x * sinTurn + WindDirection.y * cosTurn
        );
        const float wavelength = mainWavelength * wave.WavelengthScale;
        const float k = TwoPi / max(wavelength, shortestWavelength);

        // Deep water: the speed scales with the square root of the wavelength.
        const float phase = k * (dot(xz, direction) - WindSpeed * sqrt(wave.WavelengthScale) * SimulationTime);
        // Fades out as the wave gets too short for the grid.
        const float amplitude = mainAmplitude * wave.AmplitudeScale * saturate(wavelength / shortestWavelength - 1.0);
        height += amplitude * sin(phase);
        gradient += amplitude * k * cos(phase) * direction;
    }

    return height;
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
        + SwellHeight(xz, gradient)
        + RippleHeight(int2(gridPoint), rippleGradient);
    gradient += rippleGradient;

    WaveSurfaceVarying output;
    output.WorldPosition = float3(xz.x, height, xz.y);
    output.Position = mul(float4(output.WorldPosition, 1.0), ViewProjection);
    output.WorldNormal = normalize(float3(-gradient.x, 1.0, -gradient.y));
    return output;
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
    return float4(albedo * lighting + glint, 1.0);
}
