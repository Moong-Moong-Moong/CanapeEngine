#include "Simulation.hlsli"

// Mirrors WaveRipple in Source/Simulation/WaveSurfaces.cpp.
cbuffer WaveRipple : register(b1)
{
    float2 AreaOrigin;
    float2 CellSize;
    float RippleSpeed;
    float RippleDamping;
    float RippleStrength;
    uint PointsPerSide;
};

// Per second. The reflecting border keeps the mean height, so without it the random push makes the sea level drift.
static const float RippleSettleRate = 0.05;

// Height and vertical velocity at each grid point.
Texture2D<float2> RippleLatest : register(t0);
RWTexture2D<float2> RippleNext : register(u0);

uint HashUint(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// Uniform in [-1, 1), different at every point and step.
float RandomPush(int2 cell)
{
    const uint hash = HashUint(asuint(cell.x) * 73856093u ^ asuint(cell.y) * 19349663u ^ StepNumber * 83492791u);
    return float(hash >> 8) * (1.0 / 8388608.0) - 1.0;
}

// Velocity the step's disturbances give the point at xz.
float DisturbancePush(float2 xz)
{
    float push = 0.0;
    for (uint i = 0; i < WaveDisturbanceCount; ++i)
    {
        const WaveDisturbance disturbance = WaveDisturbances[i];
        const float2 offset = (xz - disturbance.Center) / disturbance.Radius;
        const float distanceSquared = dot(offset, offset);

        // The middle and the rim go opposite ways, so no water is added or lost.
        push += disturbance.Strength * (1.0 - distanceSquared) * exp(-distanceSquared);
    }

    return push;
}

// The border repeats its own height, so waves reflect there.
float HeightAt(int2 cell)
{
    const int2 last = int2(PointsPerSide, PointsPerSide) - 1;
    return RippleLatest.Load(int3(clamp(cell, int2(0, 0), last), 0)).x;
}

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= PointsPerSide))
    {
        return;
    }

    const int2 cell = int2(id.xy);
    const float2 state = RippleLatest.Load(int3(cell, 0));
    const float height = state.x;
    const float laplacian =
        (HeightAt(cell - int2(1, 0)) - 2.0 * height + HeightAt(cell + int2(1, 0))) / (CellSize.x * CellSize.x)
        + (HeightAt(cell - int2(0, 1)) - 2.0 * height + HeightAt(cell + int2(0, 1))) / (CellSize.y * CellSize.y);

    // A random walk, so it grows with the square root of the step.
    const float push = RippleStrength * RandomPush(cell) * sqrt(StepDeltaTime)
        + DisturbancePush(AreaOrigin + float2(cell) * CellSize);

    // The damping is implicit: stable for any RippleDamping * StepDeltaTime.
    const float velocity = (state.y + RippleSpeed * RippleSpeed * laplacian * StepDeltaTime + push)
        / (1.0 + RippleDamping * StepDeltaTime);
    const float nextHeight = (height + velocity * StepDeltaTime) / (1.0 + RippleSettleRate * StepDeltaTime);
    RippleNext[id.xy] = float2(nextHeight, velocity);
}
