// Flames as additive soft sprites that rise, taper and cool. Colors are linear HDR.
#include "Common/Constants.hlsli"
#include "ParticleEffect.hlsli"
#include "Simulation.hlsli"

// Filled by name from the effect's parameters; EmitterPosition comes from the engine.
cbuffer FlameMotion : register(b1)
{
    float3 EmitterPosition;
    // Meters, of the disc the flames are born on.
    float Radius;
    // Seconds, shortest and longest.
    float2 LifeRange;
    // Meters per second, upward at birth.
    float LaunchSpeed;
    // Meters per second squared, reached at the end of the life.
    float Buoyancy;
    // Per second squared, toward the emitter's axis.
    float InwardPull;
    // Meters per second squared of turbulence.
    float Sway;
    // Per second: how fast the horizontal velocity takes the wind's.
    float WindFollow;
    // Meters across at its widest.
    float FlameSize;
};

cbuffer FlameLook : register(b11)
{
    float3 CoreColor;
    float Brightness;
    float3 MidColor;
    // Height over width of a sprite.
    float Aspect;
    float3 TailColor;
};

static const float TwoPi = 6.28318530718;
static const float2 QuadCorners[6] = {
    float2(-1.0, -1.0),
    float2(1.0, -1.0),
    float2(1.0, 1.0),
    float2(-1.0, -1.0),
    float2(1.0, 1.0),
    float2(-1.0, 1.0)
};

struct FlameVarying
{
    float4 Position : SV_POSITION;
    float2 Corner : TEXCOORD0;
    float3 Color : TEXCOORD1;
};

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

Particle Born(uint slot, uint generation)
{
    const float angle = TwoPi * HashToUnit(HashParticle(slot, generation, 0u));
    // The square root spreads the births evenly over the disc.
    const float distance = Radius * sqrt(HashToUnit(HashParticle(slot, generation, 1u)));

    Particle particle;
    particle.Position = EmitterPosition + float3(cos(angle), 0.0, sin(angle)) * distance;
    particle.Life = lerp(LifeRange.x, LifeRange.y, HashToUnit(HashParticle(slot, generation, 2u)));
    particle.Velocity = float3(0.0, LaunchSpeed * (0.7 + 0.6 * HashToUnit(HashParticle(slot, generation, 3u))), 0.0);
    particle.Variation = float2(
        HashToUnit(HashParticle(slot, generation, 4u)),
        HashToUnit(HashParticle(slot, generation, 5u))
    );
    // The first births are spread over a life, so the particles do not live in step.
    particle.Age = generation == 1u ? -particle.Life * HashToUnit(HashParticle(slot, generation, 6u)) : 0.0;
    particle.Generation = generation;
    particle.Size = 0.0;
    return particle;
}

float SizeCurve(float lifeFraction)
{
    return smoothstep(0.0, 0.2, lifeFraction) * (1.0 - 0.85 * smoothstep(0.3, 1.0, lifeFraction));
}

void Rise(inout Particle particle)
{
    const float lifeFraction = saturate(particle.Age / particle.Life);
    const float2 fromAxis = particle.Position.xz - EmitterPosition.xz;
    // The noise scrolls down, so its eddies travel up the flame.
    const float2 turbulence = Turbulence(
        float3(particle.Position.x * 3.0, particle.Position.y * 2.0 - SimulationTime * 2.5, particle.Position.z * 3.0)
    );

    float2 horizontal = particle.Velocity.xz;
    horizontal += (turbulence * Sway
        - fromAxis * InwardPull
        + (WindDirection * WindSpeed - horizontal) * WindFollow) * StepDeltaTime;
    particle.Velocity = float3(
        horizontal.x,
        particle.Velocity.y + Buoyancy * lifeFraction * StepDeltaTime,
        horizontal.y
    );
    particle.Position += particle.Velocity * StepDeltaTime;
    particle.Size = FlameSize * (0.75 + 0.5 * particle.Variation.y) * SizeCurve(lifeFraction);
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
    if (particle.Age >= particle.Life)
    {
        particle = Born(id.x, particle.Generation + 1u);
    }
    else
    {
        particle.Age += StepDeltaTime;
        if (particle.Age >= 0.0)
        {
            Rise(particle);
        }
    }
    ParticleSlots[id.x] = particle;
}

float3 FlameColor(float lifeFraction)
{
    const float3 warm = lerp(CoreColor, MidColor, smoothstep(0.0, 0.3, lifeFraction));
    const float3 cooled = lerp(warm, TailColor, smoothstep(0.25, 0.8, lifeFraction));
    return cooled * smoothstep(0.0, 0.08, lifeFraction) * (1.0 - smoothstep(0.55, 1.0, lifeFraction));
}

FlameVarying vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const Particle particle = Particles[instanceId];
    const float2 corner = QuadCorners[vertexId];
    // The camera's right and up in the world: the first two columns of View.
    const float3 right = float3(View[0][0], View[1][0], View[2][0]);
    const float3 up = float3(View[0][1], View[1][1], View[2][1]);
    const float halfWidth = 0.5 * particle.Size;
    const float3 worldPosition = particle.Position
        + right * corner.x * halfWidth
        + up * corner.y * halfWidth * Aspect;

    FlameVarying output;
    output.Position = mul(float4(worldPosition, 1.0), ViewProjection);
    output.Corner = corner;
    output.Color = FlameColor(saturate(particle.Age / particle.Life))
        * (0.7 + 0.6 * particle.Variation.x)
        * Brightness;
    return output;
}

float4 ps_main(FlameVarying input) : SV_TARGET
{
    const float falloff = saturate(1.0 - dot(input.Corner, input.Corner));
    return float4(input.Color * falloff * falloff, 1.0);
}
