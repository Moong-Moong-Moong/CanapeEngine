// Meteors of one shower that burn out high in the sky. Colors are linear HDR.
// Positions and Size are in a sky around the viewer measured in heights of the meteor layer; the draw puts them at infinity.
#include "ParticleEffect.hlsli"
#include "Simulation.hlsli"
#include "Sky.hlsli"

// Filled by name from the effect's parameters.
cbuffer MeteorMotion : register(b1)
{
    // Toward the point the meteors seem to come from; any length.
    float3 RadiantDirection;
    // Births per second on average.
    float MeteorsPerSecond;
    // Layer heights, shortest and longest path a meteor burns along.
    float2 PathRange;
    // Layer heights per second.
    float Speed;
    // Radians above the horizon, of the lowest start.
    float LowestElevation;
    // Layer heights across a meteor's glow, kept in Size.
    float Width;
};

cbuffer MeteorLook : register(b11)
{
    // Of the glow around the brightest meteor one layer height away. Its middle is brighter and burns white.
    float3 Color;
};

struct MeteorVarying
{
    float4 Position : SV_POSITION;
    // In half widths of the glow: across the train, and behind the head.
    float2 Offset : TEXCOORD0;
    // The train's length in the same measure.
    float Train : TEXCOORD1;
    float3 Color : TEXCOORD2;
};

// Whether the waiting meteor in slot is born in this step.
// Burning meteors are few against count, so the waiting slots together give about MeteorsPerSecond.
bool BornThisStep(uint slot, uint count)
{
    // HashUint(0) is 0, so the hashed inputs start at one.
    const uint step = HashUint(StepNumber + 1u);
    return HashToUnit(HashUint(step ^ HashUint(slot + 1u))) < MeteorsPerSecond * StepDeltaTime / float(count);
}

// On the layer somewhere in the sky, flying away from the radiant like every other meteor of the shower.
Particle Born(uint slot, uint generation)
{
    const float angle = TwoPi * HashToUnit(HashParticle(slot, generation, 0u));
    // A uniform sine of the elevation puts as many births in every patch of sky.
    const float rise = lerp(sin(LowestElevation), 1.0, HashToUnit(HashParticle(slot, generation, 1u)));
    const float spread = sqrt(1.0 - rise * rise);
    const float path = lerp(PathRange.x, PathRange.y, HashToUnit(HashParticle(slot, generation, 2u)));

    Particle meteor = (Particle)0;
    meteor.Position = float3(cos(angle) * spread, rise, sin(angle) * spread) / rise;
    meteor.Velocity = -normalize(RadiantDirection) * Speed;
    meteor.Life = path / Speed;
    meteor.Generation = generation;
    meteor.Size = Width;
    meteor.Variation.x = HashToUnit(HashParticle(slot, generation, 3u));
    return meteor;
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

    Particle meteor = ParticleSlots[id.x];
    meteor.Age += StepDeltaTime;
    if (meteor.Age < meteor.Life)
    {
        meteor.Position += meteor.Velocity * StepDeltaTime;
    }
    else if (BornThisStep(id.x, count))
    {
        meteor = Born(id.x, meteor.Generation + 1u);
    }
    else
    {
        meteor.Size = 0.0;
    }
    ParticleSlots[id.x] = meteor;
}

MeteorVarying vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const Particle meteor = Particles[instanceId];
    if (meteor.Size == 0.0)
    {
        return (MeteorVarying)0;
    }

    const float2 corner = QuadCorners[vertexId];
    const float headDistance = length(meteor.Position);
    const float3 headDirection = meteor.Position / headDistance;
    const float lifeFraction = meteor.Age / meteor.Life;
    // The train's end leaves the start slower than the head and reaches it as the meteor burns out.
    const float trainSeconds = meteor.Age * (1.0 - lifeFraction);
    const float3 tailDirection = normalize(meteor.Position - meteor.Velocity * trainSeconds);
    // The way the head moves across the sky; none for a meteor that comes straight at the viewer.
    const float3 motion = meteor.Velocity - headDirection * dot(meteor.Velocity, headDirection);
    const float3 side = cross(headDirection, motion / max(length(motion), 1e-8));
    // A nearer meteor is wider on the sky.
    const float halfWidth = 0.5 * meteor.Size / headDistance;
    const float train = distance(headDirection, tailDirection) / halfWidth;

    const float head = 0.5 + 0.5 * corner.y;
    const float3 middle = lerp(tailDirection, headDirection, head);
    // The quad reaches half a width past both ends, so the head's glow is round.
    const float3 direction = middle + (cross(side, middle) * corner.y + side * corner.x) * halfWidth;

    // The head is as bright as the train is long, most of both halfway along the path.
    const float flare = 4.0 * lifeFraction * (1.0 - lifeFraction);
    // As many meteors in each of four stops below the brightest.
    const float intrinsic = exp2(-4.0 * meteor.Variation.x);

    MeteorVarying output;
    output.Position = SkyPosition(direction);
    output.Offset = float2(corner.x, lerp(train + 1.0, -1.0, head));
    output.Train = train;
    // It dims with the square of the distance.
    output.Color = Color * flare * intrinsic / (headDistance * headDistance);
    return output;
}

float4 ps_main(MeteorVarying input) : SV_TARGET
{
    // Zero at the head, one where the train ends.
    const float behind = saturate(input.Offset.y / max(input.Train, 1e-4));
    // The train thins toward its end.
    const float thinning = lerp(1.0, 0.25, behind);
    // From the head in front of it, from the train's line behind it.
    const float2 away = float2(input.Offset.x / thinning, min(input.Offset.y, 0.0));
    const float glow = saturate(1.0 - dot(away, away));
    const float fade = (1.0 - behind) * (1.0 - behind);
    // A soft glow, and a thin middle bright enough to burn white.
    return float4(input.Color * fade * (glow * glow + 40.0 * pow(glow, 32.0)), 1.0);
}
