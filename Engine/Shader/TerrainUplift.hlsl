#include "Terrain.hlsli"

// Mirrors TerrainUplift in Source/Simulation/TerrainGeneration.cpp.
cbuffer TerrainUplift : register(b1)
{
    uint Seed;
    float BaseHeight;
    float MaxHeight;
    float CellSize;
    uint PointsPerSide;
    float3 pad_;
};

// Bedrock height and cover thickness in meters at each grid point.
RWTexture2D<float2> Ground : register(u0);

static const float ScaleMeters = 2400.0;
static const float StrikeDegrees = 28.0;
static const float Stretch = 1.7;
static const float Warp = 0.35;
static const uint Octaves = 4;
static const float Gain = 0.42;
static const float Lacunarity = 2.1;
static const float TiltAzimuthDegrees = 215.0;
static const float TiltDrop = 0.3;
static const float DegreesToRadians = TwoPi / 360.0;

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= PointsPerSide))
    {
        return;
    }

    const float x = float(id.x) * CellSize;
    const float z = float(id.y) * CellSize;

    const float strike = StrikeDegrees * DegreesToRadians;
    const float2 offset = 16.0 * float2(
        HashToUnit(HashLattice(int2(1, 2), Seed)),
        HashToUnit(HashLattice(int2(3, 4), Seed))
    );
    float2 uv = float2(
        (x * cos(strike) + z * sin(strike)) / (ScaleMeters * Stretch),
        (-x * sin(strike) + z * cos(strike)) / ScaleMeters
    ) + offset;

    const float2 warpOffset = float2(
        Fbm2(uv * 0.7 + float2(31.7, -12.3), 2, 0.5, 2.0, Seed + 101u),
        Fbm2(uv * 0.7 + float2(-7.9, 44.1), 2, 0.5, 2.0, Seed + 102u)
    );
    uv += Warp * warpOffset;
    const float noise = Fbm2(uv, Octaves, Gain, Lacunarity, Seed);

    const float azimuth = TiltAzimuthDegrees * DegreesToRadians;
    const float size = float(PointsPerSide - 1u) * CellSize;
    const float along = ((x - size * 0.5) * cos(azimuth) + (z - size * 0.5) * sin(azimuth)) / size;
    const float reach = 0.5 * (abs(cos(azimuth)) + abs(sin(azimuth)));
    const float t = (0.5 + 0.5 * noise / NoiseBound - TiltDrop * along + TiltDrop * reach)
        / (1.0 + 2.0 * TiltDrop * reach);

    Ground[id.xy] = float2(lerp(BaseHeight, MaxHeight, t), 0.0);
}
