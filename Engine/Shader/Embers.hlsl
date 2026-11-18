// Embers as small sparks that leave the fire a few at a time and in bursts, drift with the wind and cool. Colors are linear HDR.
#include "Common/Constants.hlsli"
#include "ParticleEffect.hlsli"
#include "Simulation.hlsli"

// Filled by name from the effect's parameters; EmitterPosition comes from the engine.
cbuffer EmberMotion : register(b1)
{
    float3 EmitterPosition;
    // Meters, of the disc the embers are born on.
    float Radius;
    // Seconds, shortest and longest.
    float2 LifeRange;
    // Seconds between bursts on average.
    float BurstInterval;
    // The most of the waiting embers one burst takes, in [0, 1].
    float BurstShare;
    // Per second, the share of the waiting embers born between bursts.
    float TrickleRate;
    // Meters per second, upward at birth.
    float LaunchSpeed;
    // Meters per second, sideways at birth at most.
    float LaunchSpread;
    // Meters per second squared, upward at birth and gone at the end of the life.
    float Buoyancy;
    // Per second: how fast the velocity takes the wind's.
    float Drag;
    // Meters per second squared of turbulence.
    float Sway;
    // Meters across at birth.
    float EmberSize;
};

cbuffer EmberLook : register(b11)
{
    float3 HotColor;
    float Brightness;
    float3 CoolColor;
    // Radians per second.
    float FlickerRate;
};

struct EmberVarying
{
    float4 Position : SV_POSITION;
    float2 Corner : TEXCOORD0;
    float3 Color : TEXCOORD1;
};

// Whether the waiting ember in slot is born in this step: a few at any time, many in a burst.
bool BornThisStep(uint slot)
{
    // HashUint(0) is 0, so the hashed inputs start at one.
    const uint step = HashUint(StepNumber + 1u);
    // A step holds a burst with probability StepDeltaTime / BurstInterval.
    const bool burst = HashToUnit(step) * BurstInterval < StepDeltaTime;
    const float share = burst ?
        BurstShare * lerp(0.5, 1.0, HashToUnit(HashUint(step + 1u))) :
        TrickleRate * StepDeltaTime;
    return HashToUnit(HashUint(step ^ HashUint(slot + 1u))) < share;
}

Particle Born(uint slot, uint generation)
{
    const float angle = TwoPi * HashToUnit(HashParticle(slot, generation, 0u));
    // The square root spreads the births evenly over the disc.
    const float distance = Radius * sqrt(HashToUnit(HashParticle(slot, generation, 1u)));
    const float launchAngle = TwoPi * HashToUnit(HashParticle(slot, generation, 2u));
    const float spread = LaunchSpread * HashToUnit(HashParticle(slot, generation, 3u));
    const float lift = LaunchSpeed * (0.6 + 0.8 * HashToUnit(HashParticle(slot, generation, 4u)));

    Particle particle;
    particle.Position = EmitterPosition + float3(cos(angle), 0.0, sin(angle)) * distance;
    particle.Age = 0.0;
    particle.Velocity = float3(cos(launchAngle) * spread, lift, sin(launchAngle) * spread);
    particle.Life = lerp(LifeRange.x, LifeRange.y, HashToUnit(HashParticle(slot, generation, 5u)));
    particle.Generation = generation;
    particle.Size = 0.0;
    particle.Variation = float2(
        HashToUnit(HashParticle(slot, generation, 6u)),
        HashToUnit(HashParticle(slot, generation, 7u))
    );
    return particle;
}

void Rise(inout Particle particle, uint slot)
{
    const float lifeFraction = saturate(particle.Age / particle.Life);
    // Each ember reads the noise at its own offset, so neighbours part ways.
    const float2 turbulence = Turbulence(float3(
        particle.Position.x * 2.0 + float(slot) * 13.7,
        particle.Position.y * 1.5 - SimulationTime * 1.5,
        particle.Position.z * 2.0
    ));
    const float3 wind = float3(WindDirection.x, 0.0, WindDirection.y) * WindSpeed;
    const float3 acceleration = (wind - particle.Velocity) * Drag
        + float3(turbulence.x * Sway, Buoyancy * (1.0 - lifeFraction), turbulence.y * Sway);

    particle.Velocity += acceleration * StepDeltaTime;
    particle.Position += particle.Velocity * StepDeltaTime;
    particle.Size = EmberSize * (0.6 + 0.8 * particle.Variation.y) * (1.0 - 0.7 * smoothstep(0.5, 1.0, lifeFraction));
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

    Particle particle = ParticleSlots[id.x];
    particle.Age += StepDeltaTime;
    if (particle.Age < particle.Life)
    {
        Rise(particle, id.x);
    }
    else if (BornThisStep(id.x))
    {
        particle = Born(id.x, particle.Generation + 1u);
    }
    else
    {
        particle.Size = 0.0;
    }
    ParticleSlots[id.x] = particle;
}

float3 EmberColor(float lifeFraction)
{
    return lerp(HotColor, CoolColor, smoothstep(0.15, 0.9, lifeFraction))
        * smoothstep(0.0, 0.05, lifeFraction)
        * (1.0 - smoothstep(0.7, 1.0, lifeFraction));
}

EmberVarying vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const Particle particle = Particles[instanceId];
    const float2 corner = QuadCorners[vertexId];
    // The camera's right and up in the world: the first two columns of View.
    const float3 right = float3(View[0][0], View[1][0], View[2][0]);
    const float3 up = float3(View[0][1], View[1][1], View[2][1]);
    // The sprite is stretched along its motion across the screen.
    const float2 screenVelocity = float2(dot(particle.Velocity, right), dot(particle.Velocity, up));
    const float speed = length(screenVelocity);
    const float2 along = speed > 1e-4 ? screenVelocity / speed : float2(0.0, 1.0);
    const float stretch = 1.0 + min(speed * 0.8, 2.5);
    const float2 offset = (along * corner.x * stretch + float2(-along.y, along.x) * corner.y) * 0.5 * particle.Size;
    const float flicker = lerp(0.35, 1.0, pow(0.5 + 0.5 * sin(particle.Age * FlickerRate + TwoPi * particle.Variation.x), 2.0));

    EmberVarying output;
    output.Position = mul(float4(particle.Position + right * offset.x + up * offset.y, 1.0), ViewProjection);
    output.Corner = corner;
    output.Color = EmberColor(saturate(particle.Age / particle.Life)) * flicker * Brightness;
    return output;
}

float4 ps_main(EmberVarying input) : SV_TARGET
{
    const float falloff = saturate(1.0 - dot(input.Corner, input.Corner));
    return float4(input.Color * falloff * falloff, 1.0);
}
