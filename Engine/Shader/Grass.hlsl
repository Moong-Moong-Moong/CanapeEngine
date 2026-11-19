// Grass as blades that lean with the wind and tremble each on its own. The effect's map says where they
// grow: its red is the cover, its green the dryness.
// Position is the root, Size the height, Variation (dryness, flexibility), Custom (bend xz, bend velocity xz).
// A bend is the tip's offset over the blade's height.
#include "Common/Lighting.hlsli"
#include "ParticleEffect.hlsli"
#include "Simulation.hlsli"

// Filled by name from the effect's parameters; EmitterPosition comes from the engine and is on the ground.
cbuffer GrassMotion : register(b1)
{
    float3 EmitterPosition;
    // Meters from the emitter to each edge of the map.
    float MapHalfExtent;
    // Meters, shortest and tallest where the cover is full.
    float2 HeightRange;
    // The bend one meter per second of wind holds a blade at.
    float WindLean;
    // Of that bend: how far a blade trembles around it.
    float Flutter;
    // Radians per second, of the middle blade.
    float FlutterRate;
    // The largest bend a blade rests at, in its own direction, when the air is still.
    float RestLean;
    // Per second squared: how fast a blade takes up its bend.
    float Stiffness;
    // The largest bend.
    float MaxBend;
};

cbuffer GrassLook : register(b11)
{
    float3 RootColor;
    float3 TipColor;
    // The tip of the driest blade.
    float3 DryColor;
    // Meters, narrowest and widest at the root.
    float2 WidthRange;
};

struct GrassVarying
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float3 Albedo : TEXCOORD2;
};

// How many places a slot tries before it stays empty.
static const uint PlantAttempts = 8u;
static const uint Segments = 3u;
// A segment's two triangles, as corners across [-1, 1] and up [0, 1].
static const float2 SegmentCorners[6] = {
    float2(-1.0, 0.0),
    float2(1.0, 0.0),
    float2(1.0, 1.0),
    float2(-1.0, 0.0),
    float2(1.0, 1.0),
    float2(-1.0, 1.0)
};
// How far a blade's normal turns up, so it is lit like the ground it covers.
static const float NormalTilt = 0.5;

// The map at uv in [0, 1]^2, between its four nearest cells.
float4 MapAt(float2 uv)
{
    uint width;
    uint height;
    EffectMap.GetDimensions(width, height);

    const int2 last = int2(width, height) - 1;
    const float2 cell = uv * float2(width, height) - 0.5;
    const int2 base = int2(floor(cell));
    const float2 weight = cell - floor(cell);
    const float4 top = lerp(
        EffectMap.Load(int3(clamp(base, 0, last), 0)),
        EffectMap.Load(int3(clamp(base + int2(1, 0), 0, last), 0)),
        weight.x
    );
    const float4 bottom = lerp(
        EffectMap.Load(int3(clamp(base + int2(0, 1), 0, last), 0)),
        EffectMap.Load(int3(clamp(base + int2(1, 1), 0, last), 0)),
        weight.x
    );
    return lerp(top, bottom, weight.y);
}

float2 ClampBend(float2 bend)
{
    const float length2 = dot(bend, bend);
    return length2 > MaxBend * MaxBend ? bend * (MaxBend * rsqrt(length2)) : bend;
}

// Where the blade settles: its own lean, the wind's, and its tremble around the wind's.
float2 TargetBend(Particle blade, uint slot)
{
    const float restAngle = TwoPi * HashToUnit(HashParticle(slot, 0u, 0u));
    const float2 rest = RestLean * HashToUnit(HashParticle(slot, 0u, 1u)) * float2(cos(restAngle), sin(restAngle));

    // The rate and the phase are the blade's own, so neighbours do not move together.
    const float phase = TwoPi * HashToUnit(HashParticle(slot, 0u, 2u));
    const float angle = SimulationTime * FlutterRate * lerp(0.6, 1.4, HashToUnit(HashParticle(slot, 0u, 3u))) + phase;
    // Rates that never line up, swelling and fading; a blade trembles mostly along the wind.
    const float swell = 0.6 + 0.4 * sin(0.31 * angle + 3.0 * phase);
    const float along = swell * (0.65 * sin(angle) + 0.35 * sin(2.3 * angle + phase));
    const float across = swell * 0.4 * sin(1.7 * angle + 2.0 * phase);
    const float2 crosswind = float2(-WindDirection.y, WindDirection.x);
    const float2 wind = WindDirection * (1.0 + Flutter * along) + crosswind * (Flutter * across);

    const float flexibility = lerp(0.7, 1.3, blade.Variation.y);
    return ClampBend(rest + wind * (WindLean * WindSpeed * flexibility));
}

// The blade of slot, without height when the map lets none of its attempts grow.
Particle Planted(uint slot)
{
    Particle blade = (Particle)0;
    blade.Generation = 1u;
    // An attempt is a place anywhere on the map, kept as often as the cover there says,
    // so the blades spread as the cover does.
    [loop]
    for (uint attempt = 1u; attempt <= PlantAttempts && blade.Size == 0.0; ++attempt)
    {
        const float2 uv = float2(
            HashToUnit(HashParticle(slot, attempt, 0u)),
            HashToUnit(HashParticle(slot, attempt, 1u))
        );
        const float4 ground = MapAt(uv);
        if (HashToUnit(HashParticle(slot, attempt, 2u)) < ground.r)
        {
            blade.Position = EmitterPosition + float3(uv.x * 2.0 - 1.0, 0.0, uv.y * 2.0 - 1.0) * MapHalfExtent;
            // Thin cover grows short.
            blade.Size = lerp(HeightRange.x, HeightRange.y, HashToUnit(HashParticle(slot, attempt, 3u)))
                * lerp(0.5, 1.0, ground.r);
            blade.Variation = float2(
                saturate(ground.g + 0.3 * HashToSigned(HashParticle(slot, attempt, 4u))),
                HashToUnit(HashParticle(slot, attempt, 5u))
            );
        }
    }
    blade.Custom = float4(TargetBend(blade, slot), 0.0, 0.0);
    return blade;
}

void Bend(inout Particle blade, uint slot)
{
    float2 bend = blade.Custom.xy;
    float2 bendVelocity = blade.Custom.zw;
    // Damped just enough that the blade reaches its target without swinging past it.
    bendVelocity += (Stiffness * (TargetBend(blade, slot) - bend) - 2.0 * sqrt(Stiffness) * bendVelocity)
        * StepDeltaTime;
    bend += bendVelocity * StepDeltaTime;
    blade.Custom = float4(bend, bendVelocity);
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

    Particle blade = ParticleSlots[id.x];
    if (blade.Generation == 0u)
    {
        blade = Planted(id.x);
    }
    else if (blade.Size > 0.0)
    {
        Bend(blade, id.x);
    }
    ParticleSlots[id.x] = blade;
}

// The slope of the unit-height centreline at t in [0, 1].
float3 BladeSlope(float t, float2 bend)
{
    return float3(2.0 * bend.x * t, 1.0 - 0.6 * dot(bend, bend) * t, 2.0 * bend.y * t);
}

// Simpson's rule over the unit-height centreline, so a bent blade keeps its height as its length.
float BladeLength(float2 bend)
{
    float sum = length(BladeSlope(0.0, bend)) + length(BladeSlope(1.0, bend));
    sum += 4.0 * (length(BladeSlope(0.25, bend)) + length(BladeSlope(0.75, bend)));
    sum += 2.0 * length(BladeSlope(0.5, bend));
    return sum / 12.0;
}

// The centreline at t in [0, 1]: a quadratic lean toward the bend.
float3 BladeCurve(float t, float height, float2 bend)
{
    const float shorten = 1.0 - 0.3 * dot(bend, bend) * t;
    return float3(bend.x * t * t, t * shorten, bend.y * t * t) * (height / BladeLength(bend));
}

GrassVarying vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const Particle blade = Particles[instanceId];
    const float2 corner = SegmentCorners[vertexId % 6u];
    const float t = (float(vertexId / 6u) + corner.y) / float(Segments);
    const float2 bend = blade.Custom.xy;
    const float rootWidth = blade.Size > 0.0 ?
        lerp(WidthRange.x, WidthRange.y, HashToUnit(HashParticle(instanceId, 0u, 4u))) :
        0.0;

    // The blade turns about Y to face the camera.
    const float3 toCamera = CameraPosition - blade.Position;
    const float2 facing = toCamera.xz / max(length(toCamera.xz), 1e-4);
    const float3 side = float3(facing.y, 0.0, -facing.x);
    float3 normal = normalize(cross(side, BladeSlope(t, bend)));
    normal = dot(normal, toCamera) < 0.0 ? -normal : normal;
    normal = normalize(lerp(normal, float3(0.0, 1.0, 0.0), NormalTilt));

    const float3 world = blade.Position
        + BladeCurve(t, blade.Size, bend)
        + side * corner.x * 0.5 * rootWidth * (1.0 - t * t);

    GrassVarying output;
    output.Position = mul(float4(world, 1.0), ViewProjection);
    output.WorldPosition = world;
    output.Normal = normal;
    output.Albedo = lerp(RootColor, lerp(TipColor, DryColor, blade.Variation.x), t);
    return output;
}

float4 ps_main(GrassVarying input) : SV_TARGET
{
    const float3 lighting = AmbientColor * AmbientIntensity
        + CalculateDirectLighting(input.WorldPosition, normalize(input.Normal));
    return float4(input.Albedo * lighting, 1.0);
}
