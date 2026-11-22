#ifndef CANAPE_PARTICLE_EFFECT_HLSLI
#define CANAPE_PARTICLE_EFFECT_HLSLI

#include "Simulation.hlsli"

// As ParticleStride in Source/Simulation/ParticleEffects.cpp says.
struct Particle
{
    float3 Position;
    // Seconds since birth; negative while it waits for its first birth.
    float Age;
    float3 Velocity;
    // Seconds. A particle that old is born again.
    float Life;
    // Births so far; zero only before the first.
    uint Generation;
    // Meters across; zero is not drawn.
    float Size;
    // Uniform in [0, 1), drawn at birth.
    float2 Variation;
    // What the effect keeps beyond the fields above, as its shader says.
    float4 Custom;
};

// What a draw reads, one instance per particle.
StructuredBuffer<Particle> Particles : register(t8);
// What a kernel rewrites in place. All zero before the first step.
RWStructuredBuffer<Particle> ParticleSlots : register(u0);
// What a kernel reads when its effect has a map.
Texture2D<float4> EffectMap : register(t1);

uint HashUint(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// An independent value for each k of one birth of one particle.
uint HashParticle(uint slot, uint generation, uint k)
{
    const uint hash = HashUint(HashUint(slot) ^ (generation * 0x9e3779b9u));
    return HashUint(hash + k * 0x85ebca6bu);
}

// Uniform in [0, 1).
float HashToUnit(uint hash)
{
    return float(hash >> 8) * (1.0 / 16777216.0);
}

float HashToSigned(uint hash)
{
    return HashToUnit(hash) * 2.0 - 1.0;
}

float2 LatticeVector(int3 cell)
{
    const uint hash = HashUint(asuint(cell.x) * 73856093u ^ asuint(cell.y) * 19349663u ^ asuint(cell.z) * 83492791u);
    return float2(HashToSigned(hash), HashToSigned(HashUint(hash)));
}

// Smooth noise in [-1, 1] per component.
float2 Turbulence(float3 position)
{
    const float3 base = floor(position);
    const float3 fraction = position - base;
    const float3 weight = fraction * fraction * (3.0 - 2.0 * fraction);
    const int3 cell = int3(base);
    const float2 x00 = lerp(LatticeVector(cell + int3(0, 0, 0)), LatticeVector(cell + int3(1, 0, 0)), weight.x);
    const float2 x10 = lerp(LatticeVector(cell + int3(0, 1, 0)), LatticeVector(cell + int3(1, 1, 0)), weight.x);
    const float2 x01 = lerp(LatticeVector(cell + int3(0, 0, 1)), LatticeVector(cell + int3(1, 0, 1)), weight.x);
    const float2 x11 = lerp(LatticeVector(cell + int3(0, 1, 1)), LatticeVector(cell + int3(1, 1, 1)), weight.x);
    return lerp(lerp(x00, x10, weight.y), lerp(x01, x11, weight.y), weight.z);
}

// A sprite's two triangles, as corners of [-1, 1]^2.
static const float2 QuadCorners[6] = {
    float2(-1.0, -1.0),
    float2(1.0, -1.0),
    float2(1.0, 1.0),
    float2(-1.0, -1.0),
    float2(1.0, 1.0),
    float2(-1.0, 1.0)
};

#endif
