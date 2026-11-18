#ifndef CANAPE_PARTICLE_EFFECT_HLSLI
#define CANAPE_PARTICLE_EFFECT_HLSLI

// 48 bytes, as ParticleStride in Source/Simulation/ParticleEffects.cpp says.
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
};

// What a draw reads, one instance per particle.
StructuredBuffer<Particle> Particles : register(t8);
// What a kernel rewrites in place. All zero before the first step.
RWStructuredBuffer<Particle> ParticleSlots : register(u0);

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

#endif
