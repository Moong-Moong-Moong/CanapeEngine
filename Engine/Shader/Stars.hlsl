// Stars that stay where they are in the sky, dim toward the horizon and twinkle more the lower they stand. Colors are linear HDR.
// Position is the star's direction, which the draw puts at infinity; Size is its width across the sky in radians.
// Custom.x is how bright its middle is against the faintest star's.
#include "ParticleEffect.hlsli"
#include "Simulation.hlsli"
#include "Sky.hlsli"

// Filled by name from the effect's parameters.
cbuffer StarField : register(b1)
{
    // Radians across the faintest star. A brighter star is wider.
    float FaintestWidth;
};

cbuffer StarLook : register(b11)
{
    // Of the middle of the faintest star straight overhead; each star is somewhere between the two.
    float3 CoolColor;
    // Stops a star dims for each air mass its light crosses beyond the one straight overhead.
    float Extinction;
    float3 WarmColor;
    // Radians per second.
    float TwinkleRate;
    // The share of its light a star at the horizon loses at the dimmest of a twinkle.
    float Twinkle;
};

struct StarVarying
{
    float4 Position : SV_POSITION;
    float2 Corner : TEXCOORD0;
    float3 Color : TEXCOORD1;
};

// Anywhere above the horizon, as many in every patch of sky.
Particle Planted(uint slot)
{
    const float angle = TwoPi * HashToUnit(HashParticle(slot, 0u, 0u));
    const float rise = HashToUnit(HashParticle(slot, 0u, 1u));
    const float spread = sqrt(1.0 - rise * rise);
    // Halving the light doubles the stars that have at least that much, up to 256 times the faintest's.
    const float light = 1.0 / lerp(1.0 / 256.0, 1.0, HashToUnit(HashParticle(slot, 0u, 2u)));

    Particle star = (Particle)0;
    star.Position = float3(cos(angle) * spread, rise, sin(angle) * spread);
    star.Generation = 1u;
    // More light shows as a wider and a brighter star: the light is the width squared times the middle.
    star.Size = FaintestWidth * sqrt(sqrt(light));
    star.Variation = float2(
        HashToUnit(HashParticle(slot, 0u, 3u)),
        HashToUnit(HashParticle(slot, 0u, 4u))
    );
    star.Custom.x = sqrt(light);
    return star;
}

[numthreads(64, 1, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    uint count;
    uint stride;
    ParticleSlots.GetDimensions(count, stride);
    if (id.x >= count)
    {
        return;
    }

    if (ParticleSlots[id.x].Generation == 0u)
    {
        ParticleSlots[id.x] = Planted(id.x);
    }
}

StarVarying vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const Particle star = Particles[instanceId];
    const float2 corner = QuadCorners[vertexId];
    // The camera's right and up in the world: the first two columns of View.
    const float3 right = float3(View[0][0], View[1][0], View[2][0]);
    const float3 up = float3(View[0][1], View[1][1], View[2][1]);

    // The sine of the elevation; straight overhead the light crosses the air once.
    const float rise = max(star.Position.y, 1e-4);
    const float throughAir = exp2(-Extinction * (1.0 / rise - 1.0));
    // Each star at its own rate and phase, with rates that never line up.
    const float angle = SimulationTime * TwinkleRate * lerp(0.6, 1.4, star.Variation.x) + TwoPi * star.Variation.y;
    const float dimming = 0.5 + 0.5 * sin(angle) * sin(1.7 * angle + TwoPi * star.Variation.x);
    // The air makes the twinkle, so there is none straight overhead.
    const float twinkle = 1.0 - Twinkle * (1.0 - rise) * dimming;

    StarVarying output;
    output.Position = SkyPosition(star.Position + (right * corner.x + up * corner.y) * 0.5 * star.Size);
    output.Corner = corner;
    output.Color = lerp(WarmColor, CoolColor, star.Variation.y) * star.Custom.x * throughAir * twinkle;
    return output;
}

float4 ps_main(StarVarying input) : SV_TARGET
{
    const float falloff = saturate(1.0 - dot(input.Corner, input.Corner));
    // A small middle with a faint glow around it, so a bright star is not a flat disc.
    const float middle = falloff * falloff * falloff;
    return float4(input.Color * middle * middle, 1.0);
}
