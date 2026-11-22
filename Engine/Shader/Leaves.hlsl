// Leaves that let go of a tree crown, flutter down with the wind, and rest on the ground or ride the swell.
// The emitter is the crown: a sphere of diameter EmitterScale around EmitterPosition.
// Custom is (state, flutter phase, flutter rate, age at landing).
#include "Common/Lighting.hlsli"
#include "ParticleEffect.hlsli"
#include "Simulation.hlsli"
#include "Swell.hlsli"

// Filled by name from the effect's parameters; EmitterPosition and EmitterScale come from the engine.
cbuffer LeafMotion : register(b1)
{
    float3 EmitterPosition;
    // Per second, the share of the waiting leaves that let go when the air is still.
    float CalmDetachRate;
    float3 EmitterScale;
    // Per second per meter per second of wind, added to CalmDetachRate.
    float WindDetachRate;
    // Per second: how fast the velocity takes the wind's.
    // A leaf falls through still air at Gravity over this.
    float Drag;
    // Meters per second of the side-to-side swing.
    float Flutter;
    // Radians per second of the swing, of the middle leaf.
    float FlutterRate;
    // Seconds a leaf rests on the ground or the sea before it is gone, on average.
    float RestSeconds;
    // Meters, smallest and largest leaf.
    float2 SizeRange;
    float2 GroundCenter;
    // Meters, of the disc of ground under the trees.
    float GroundRadius;
    float GroundHeight;
    // The sea's height without the swell.
    float SeaHeight;
    // Of the wind's speed: how fast a leaf drifts on the sea.
    float SeaDrift;
};

cbuffer LeafLook : register(b11)
{
    float3 FreshColor;
    // In [0, 1]: how many of the leaves have turned.
    float TurnedShare;
    float3 TurnedColor;
    float3 DryColor;
};

struct LeafVarying
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float3 Albedo : TEXCOORD2;
    float2 Corner : TEXCOORD3;
};

static const float Waiting = 0.0;
static const float Falling = 1.0;
static const float Grounded = 2.0;
static const float Floating = 3.0;
// A leaf rides no wave shorter than this.
static const float ShortestRiddenWavelength = 1.0;
// Width over length.
static const float LeafAspect = 0.6;
// Radians a falling leaf tips to each side.
static const float MaxTilt = 1.1;
// Meters a resting leaf lies above the ground, so it does not flicker into it.
static const float RestLift = 0.01;
static const float GrowSeconds = 0.3;
static const float FadeSeconds = 1.0;

// Each crown draws its own leaves, though the trees share the effect.
uint EmitterSeed()
{
    return HashUint(asuint(EmitterPosition.x) * 73856093u ^ asuint(EmitterPosition.y) * 19349663u ^ asuint(EmitterPosition.z) * 83492791u);
}

uint LeafHash(uint slot, uint generation, uint k)
{
    return HashParticle(slot ^ EmitterSeed(), generation, k);
}

// Whether the waiting leaf in slot lets go in this step.
bool LetsGo(uint slot)
{
    // HashUint(0) is 0, so the step number starts at one.
    const uint step = HashUint(StepNumber + 1u);
    const float rate = CalmDetachRate + WindDetachRate * WindSpeed;
    return HashToUnit(HashUint(step ^ HashUint(slot ^ EmitterSeed()))) < rate * StepDeltaTime;
}

Particle Detached(uint slot, uint generation)
{
    // On the lower part of the crown, where the leaves hang free of it.
    const float up = lerp(-1.0, 0.3, HashToUnit(LeafHash(slot, generation, 0u)));
    const float around = TwoPi * HashToUnit(LeafHash(slot, generation, 1u));
    const float ring = sqrt(1.0 - up * up);
    const float3 direction = float3(ring * cos(around), up, ring * sin(around));

    Particle leaf = (Particle)0;
    leaf.Position = EmitterPosition + 0.5 * EmitterScale * direction;
    leaf.Generation = generation;
    leaf.Size = lerp(SizeRange.x, SizeRange.y, HashToUnit(LeafHash(slot, generation, 2u)));
    leaf.Variation = float2(
        HashToUnit(LeafHash(slot, generation, 3u)),
        HashToUnit(LeafHash(slot, generation, 4u))
    );
    leaf.Custom = float4(
        Falling,
        TwoPi * HashToUnit(LeafHash(slot, generation, 5u)),
        FlutterRate * lerp(0.7, 1.3, HashToUnit(LeafHash(slot, generation, 6u))),
        0.0
    );
    return leaf;
}

bool OverGround(float2 xz)
{
    return distance(xz, GroundCenter) <= GroundRadius;
}

float SeaSurface(float2 xz)
{
    float2 gradient;
    return SeaHeight + SwellHeight(xz, ShortestRiddenWavelength, gradient);
}

void Land(inout Particle leaf, uint slot, float state, float height)
{
    leaf.Position.y = height;
    leaf.Velocity = float3(0.0, 0.0, 0.0);
    leaf.Life = leaf.Age + RestSeconds * lerp(0.7, 1.3, HashToUnit(LeafHash(slot, leaf.Generation, 7u)));
    leaf.Custom.x = state;
    leaf.Custom.w = leaf.Age;
}

void Fall(inout Particle leaf, uint slot)
{
    leaf.Custom.y += leaf.Custom.z * StepDeltaTime;
    const float phase = leaf.Custom.y;

    const float3 wind = float3(WindDirection.x, 0.0, WindDirection.y) * WindSpeed;
    leaf.Velocity += ((wind - leaf.Velocity) * Drag - float3(0.0, Gravity, 0.0)) * StepDeltaTime;

    const float2 swing = Flutter * float2(sin(phase), cos(0.73 * phase + 1.3));
    leaf.Position += (leaf.Velocity + float3(swing.x, 0.0, swing.y)) * StepDeltaTime;

    if (OverGround(leaf.Position.xz))
    {
        if (leaf.Position.y <= GroundHeight)
        {
            Land(leaf, slot, Grounded, GroundHeight);
        }
        return;
    }

    const float sea = SeaSurface(leaf.Position.xz);
    if (leaf.Position.y <= sea)
    {
        Land(leaf, slot, Floating, sea);
    }
}

void Float(inout Particle leaf)
{
    const float2 drifted = leaf.Position.xz + WindDirection * WindSpeed * SeaDrift * StepDeltaTime;
    // A leaf stops at the shore rather than drift up onto it.
    if (!OverGround(drifted))
    {
        leaf.Position.xz = drifted;
    }
    leaf.Position.y = SeaSurface(leaf.Position.xz);
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

    Particle leaf = ParticleSlots[id.x];
    const float state = leaf.Custom.x;
    if (state == Waiting)
    {
        if (LetsGo(id.x))
        {
            leaf = Detached(id.x, leaf.Generation + 1u);
        }
        ParticleSlots[id.x] = leaf;
        return;
    }

    leaf.Age += StepDeltaTime;
    if (state == Falling)
    {
        Fall(leaf, id.x);
    }
    else if (leaf.Age >= leaf.Life)
    {
        leaf.Size = 0.0;
        leaf.Custom.x = Waiting;
    }
    else if (state == Floating)
    {
        Float(leaf);
    }
    ParticleSlots[id.x] = leaf;
}

float3 RotateAround(float3 v, float3 axis, float angle)
{
    float s;
    float c;
    sincos(angle, s, c);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}

LeafVarying vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const Particle leaf = Particles[instanceId];
    const float2 corner = QuadCorners[vertexId];
    const float state = leaf.Custom.x;
    const float phase = leaf.Custom.y;
    const bool resting = state == Grounded || state == Floating;

    const float yaw = TwoPi * leaf.Variation.y + (resting ? 0.0 : 0.5 * phase);
    float3 right = float3(cos(yaw), 0.0, sin(yaw));
    float3 forward = float3(-sin(yaw), 0.0, cos(yaw));
    float3 lift = float3(0.0, RestLift, 0.0);
    if (state == Falling)
    {
        const float tiltAngle = TwoPi * frac(leaf.Variation.y * 7.31 + 0.37);
        const float3 tiltAxis = float3(cos(tiltAngle), 0.0, sin(tiltAngle));
        right = RotateAround(right, tiltAxis, MaxTilt * sin(phase));
        forward = RotateAround(forward, tiltAxis, MaxTilt * sin(phase));
        lift = float3(0.0, 0.0, 0.0);
    }
    else if (state == Floating)
    {
        // Lies along the swell.
        float2 gradient;
        SwellHeight(leaf.Position.xz, ShortestRiddenWavelength, gradient);
        const float3 surfaceNormal = normalize(float3(-gradient.x, 1.0, -gradient.y));
        right = normalize(right - surfaceNormal * dot(right, surfaceNormal));
        forward = cross(right, surfaceNormal);
        lift = surfaceNormal * RestLift;
    }

    const float restTime = leaf.Life - leaf.Custom.w;
    const float fade = resting ? smoothstep(0.0, FadeSeconds, leaf.Life - leaf.Age) : 1.0;
    const float scale = leaf.Size * smoothstep(0.0, GrowSeconds, leaf.Age) * fade;
    const float3 world = leaf.Position + lift
        + (right * corner.x * LeafAspect + forward * corner.y) * 0.5 * scale;

    float3 normal = cross(forward, right);
    normal = dot(normal, CameraPosition - world) < 0.0 ? -normal : normal;

    // Most leaves are green and a few have turned; each is a little lighter or darker than the next.
    const float3 fresh = (leaf.Variation.x < TurnedShare ? TurnedColor : FreshColor)
        * lerp(0.8, 1.2, frac(leaf.Variation.x * 13.7));
    const float dried = resting ? saturate((leaf.Age - leaf.Custom.w) / max(restTime, 1e-3)) : 0.0;

    LeafVarying output;
    output.Position = mul(float4(world, 1.0), ViewProjection);
    output.WorldPosition = world;
    output.Normal = normal;
    output.Albedo = lerp(fresh, DryColor, dried);
    output.Corner = corner;
    return output;
}

float4 ps_main(LeafVarying input) : SV_TARGET
{
    // A leaf's outline: widest in the middle, pointed at both ends.
    const float halfWidth = 1.0 - input.Corner.y * input.Corner.y;
    clip(halfWidth - abs(input.Corner.x));

    const float midrib = saturate(1.0 - abs(input.Corner.x) / 0.08);
    const float veins = smoothstep(0.38, 0.5, abs(frac(input.Corner.y * 4.0 - abs(input.Corner.x) * 2.5) - 0.5));
    const float3 albedo = input.Albedo * (1.0 + 0.5 * midrib + 0.25 * veins);
    const float3 lighting = AmbientColor * AmbientIntensity
        + CalculateDirectLighting(input.WorldPosition, normalize(input.Normal));
    return float4(albedo * lighting, 1.0);
}
