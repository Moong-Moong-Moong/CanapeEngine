// Rain that falls from a box with the wind and spreads a ring where it lands on the ground or the sea. Colors are linear HDR.
// The emitter is the box: EmitterScale across around EmitterPosition.
// Custom is (state, unused, unused, age at landing).
#include "Common/Constants.hlsli"
#include "ParticleEffect.hlsli"
#include "Simulation.hlsli"
#include "Swell.hlsli"

// Filled by name from the effect's parameters; EmitterPosition and EmitterScale come from the engine.
cbuffer RainMotion : register(b1)
{
    float3 EmitterPosition;
    // Per second: how fast the velocity takes the wind's.
    // A drop falls through still air at Gravity over this.
    float Drag;
    float3 EmitterScale;
    // Seconds a landed drop's ring spreads before the drop falls again.
    float SplashSeconds;
    float2 GroundCenter;
    // Meters, of the disc of ground under the rain.
    float GroundRadius;
    float GroundHeight;
    // The sea's height without the swell.
    float SeaHeight;
    // Meters across a falling drop's streak, kept in Size.
    float StreakWidth;
};

cbuffer RainLook : register(b11)
{
    float3 StreakColor;
    // Meters, of the middle drop's streak.
    float StreakLength;
    float3 SplashColor;
    // Meters, of the middle drop's ring at its widest.
    float SplashRadius;
};

struct RainVarying
{
    float4 Position : SV_POSITION;
    float2 Corner : TEXCOORD0;
    float3 Color : TEXCOORD1;
    // One on a ring, zero on a streak.
    float Ring : TEXCOORD2;
};

static const float Falling = 0.0;
static const float OnGround = 1.0;
static const float OnSea = 2.0;
// A ring rides no wave shorter than this.
static const float ShortestRiddenWavelength = 1.0;
// Meters a ring lies above the surface, so it does not flicker into it.
static const float RingLift = 0.02;
// Meters from the camera over which a streak fades in, so a drop at the lens does not smear across the frame.
static const float StreakFadeNear = 1.0;
static const float StreakFadeFar = 4.0;

// The first drops fill the box; later ones start at its top. Each lands inside the box's floor.
Particle Born(uint slot, uint generation)
{
    const float fallSpeed = Gravity / Drag;
    const float3 velocity = float3(WindDirection.x * WindSpeed, -fallSpeed, WindDirection.y * WindSpeed);
    const float2 across = float2(
        HashToUnit(HashParticle(slot, generation, 0u)),
        HashToUnit(HashParticle(slot, generation, 1u))
    ) - 0.5;
    const float height = generation == 1u ? HashToUnit(HashParticle(slot, generation, 2u)) : 1.0;
    const float3 landing = EmitterPosition + EmitterScale * float3(across.x, -0.5, across.y);

    Particle drop = (Particle)0;
    // Back along its path from where it lands, by the time it takes to fall that high.
    drop.Position = landing - velocity * (height * EmitterScale.y / fallSpeed);
    drop.Velocity = velocity;
    drop.Generation = generation;
    drop.Size = StreakWidth;
    drop.Variation = float2(
        HashToUnit(HashParticle(slot, generation, 3u)),
        HashToUnit(HashParticle(slot, generation, 4u))
    );
    drop.Custom.x = Falling;
    return drop;
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

void Land(inout Particle drop, float state, float height)
{
    drop.Position.y = height;
    drop.Velocity = float3(0.0, 0.0, 0.0);
    drop.Life = drop.Age + SplashSeconds;
    drop.Custom.x = state;
    drop.Custom.w = drop.Age;
}

void Fall(inout Particle drop)
{
    const float3 wind = float3(WindDirection.x, 0.0, WindDirection.y) * WindSpeed;
    drop.Velocity += ((wind - drop.Velocity) * Drag - float3(0.0, Gravity, 0.0)) * StepDeltaTime;
    drop.Position += drop.Velocity * StepDeltaTime;

    if (OverGround(drop.Position.xz))
    {
        if (drop.Position.y <= GroundHeight)
        {
            Land(drop, OnGround, GroundHeight);
        }
        return;
    }

    const float sea = SeaSurface(drop.Position.xz);
    if (drop.Position.y <= sea)
    {
        Land(drop, OnSea, sea);
    }
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

    Particle drop = ParticleSlots[id.x];
    const bool splashed = drop.Custom.x != Falling && drop.Age >= drop.Life;
    if (drop.Generation == 0u || splashed)
    {
        drop = Born(id.x, drop.Generation + 1u);
    }

    drop.Age += StepDeltaTime;
    const float state = drop.Custom.x;
    if (state == Falling)
    {
        Fall(drop);
    }
    else if (state == OnSea)
    {
        // The ring rides the swell while it spreads.
        drop.Position.y = SeaSurface(drop.Position.xz);
    }
    ParticleSlots[id.x] = drop;
}

RainVarying vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const Particle drop = Particles[instanceId];
    const float2 corner = QuadCorners[vertexId];
    const float spread = lerp(0.7, 1.3, drop.Variation.x);

    RainVarying output;
    output.Corner = corner;
    if (drop.Custom.x == Falling)
    {
        const float speed = length(drop.Velocity);
        const float3 along = speed > 1e-4 ? drop.Velocity / speed : float3(0.0, -1.0, 0.0);
        // The streak turns its face to the camera around its own line.
        const float3 across = cross(along, CameraPosition - drop.Position);
        const float3 side = across / max(length(across), 1e-4);
        // Brightest at the drop, gone at the tail it leaves behind.
        const float head = 0.5 + 0.5 * corner.y;
        const float3 world = drop.Position
            - along * (1.0 - head) * StreakLength * spread
            + side * corner.x * 0.5 * drop.Size;

        output.Position = mul(float4(world, 1.0), ViewProjection);
        output.Color = StreakColor * head * smoothstep(StreakFadeNear, StreakFadeFar, distance(CameraPosition, world));
        output.Ring = 0.0;
        return output;
    }

    const float spreadTime = saturate((drop.Age - drop.Custom.w) / max(drop.Life - drop.Custom.w, 1e-3));
    const float radius = SplashRadius * spread * lerp(0.2, 1.0, sqrt(spreadTime));
    const float3 world = drop.Position + float3(corner.x * radius, RingLift, corner.y * radius);

    output.Position = mul(float4(world, 1.0), ViewProjection);
    output.Color = SplashColor * (1.0 - spreadTime) * (1.0 - spreadTime);
    output.Ring = 1.0;
    return output;
}

float4 ps_main(RainVarying input) : SV_TARGET
{
    const float streak = 1.0 - input.Corner.x * input.Corner.x;
    // A thin band near the rim.
    const float band = saturate(1.0 - abs(length(input.Corner) - 0.8) / 0.2);
    return float4(input.Color * lerp(streak, band * band, input.Ring), 1.0);
}
