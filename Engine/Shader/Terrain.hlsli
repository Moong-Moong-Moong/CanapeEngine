#ifndef CANAPE_TERRAIN_HLSLI
#define CANAPE_TERRAIN_HLSLI

#include "Simulation.hlsli"

uint HashLattice(int2 lattice, uint seed)
{
    uint h = asuint(lattice.x) * 0x27d4eb2du + asuint(lattice.y) * 0x165667b1u + seed * 0x9e3779b1u;
    h = (h ^ (h >> 15)) * 0x2c1b3c6du;
    h = (h ^ (h >> 12)) * 0x297a2d39u;
    return h ^ (h >> 15);
}

// Uniform in [0, 1).
float HashToUnit(uint hash)
{
    return float(hash >> 8) * (1.0 / 16777216.0);
}

float2 LatticeGradient(int2 lattice, uint seed)
{
    const float angle = HashToUnit(HashLattice(lattice, seed)) * TwoPi;
    return float2(cos(angle), sin(angle));
}

static const float NoiseGain = 1.5;
// GradientNoise2 stays within [-NoiseBound, NoiseBound]; sqrt(2) / 2 bounds 2D noise with unit gradients.
static const float NoiseBound = NoiseGain * 0.70710678;

float GradientNoise2(float2 position, uint seed)
{
    const float2 floored = floor(position);
    const int2 cell = int2(floored);
    const float2 local = position - floored;
    const float2 fade = local * local * local * (local * (local * 6.0 - 15.0) + 10.0);

    const float n00 = dot(LatticeGradient(cell, seed), local);
    const float n10 = dot(LatticeGradient(cell + int2(1, 0), seed), local - float2(1.0, 0.0));
    const float n01 = dot(LatticeGradient(cell + int2(0, 1), seed), local - float2(0.0, 1.0));
    const float n11 = dot(LatticeGradient(cell + int2(1, 1), seed), local - float2(1.0, 1.0));

    return NoiseGain * lerp(lerp(n00, n10, fade.x), lerp(n01, n11, fade.x), fade.y);
}

// Within the bound of GradientNoise2.
float Fbm2(float2 position, uint octaves, float gain, float lacunarity, uint seed)
{
    float amplitude = 1.0;
    float frequency = 1.0;
    float sum = 0.0;
    float amplitudeSum = 0.0;
    for (uint i = 0; i < octaves; ++i)
    {
        sum += amplitude * GradientNoise2(position * frequency, seed + i * 131u);
        amplitudeSum += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return sum / amplitudeSum;
}

#endif
