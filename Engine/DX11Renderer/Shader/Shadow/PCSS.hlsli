#ifndef CANAPE_SHADOW_PCSS_HLSLI
#define CANAPE_SHADOW_PCSS_HLSLI

#include "ShadowConstants.hlsli"

// Fixed shadow-space pattern avoids screen-space noise as the camera moves.
static const float2 ShadowDisk[25] = {
    float2(-0.6134, 0.6175), float2(0.1700, -0.0403),
    float2(-0.2994, -0.3655), float2(0.6457, 0.4932),
    float2(-0.7410, -0.1376), float2(0.0855, 0.7980),
    float2(0.3301, -0.8265), float2(0.7690, -0.2302),
    float2(-0.1108, 0.2996), float2(-0.3190, -0.8597),
    float2(0.4136, 0.2769), float2(-0.9561, 0.2037),
    float2(0.9901, 0.0642), float2(-0.0763, -0.6079),
    float2(-0.4420, 0.0194), float2(0.5071, -0.4718),
    float2(-0.3766, 0.8986), float2(0.1280, -0.9875),
    float2(0.8964, 0.4071), float2(-0.7849, -0.5414),
    float2(0.1648, 0.5363), float2(-0.5815, -0.7245),
    float2(0.5771, 0.7788), float2(0.7958, -0.5838),
    float2(0.0, 0.0) // Center tap protects small blockers from the sparse search pattern.
};

float2 GetPcssBlockerSearchRadiusUV(uint2 mapSize)
{
    float2 orthoSize = max(ShadowOrthoSize, float2(1e-6f, 1e-6f));
    return max(BlockerSearchRadiusWorld / orthoSize, 2.0f * rcp(float2(mapSize)));
}

void AccumulateBlockerDepth(float2 sampleUV, float receiverDepth, float bias,
    uint2 mapSize, inout float totalDepth, inout uint blockerCount)
{
    if (any(sampleUV < 0.0f) || any(sampleUV >= 1.0f)) return;
    int2 pixel = int2(sampleUV * float2(mapSize));
    float depth = DirectionalShadowMap.Load(int3(pixel, 0));
    // Rasterizer bias is already present in the stored depth. The receiver
    // comparison bias prevents a surface from finding itself as a blocker.
    if (depth < receiverDepth - bias)
    {
        totalDepth += depth;
        ++blockerCount;
    }
}

bool FindAverageBlockerDepth(float2 uv, float receiverDepth, float bias,
    float2 searchRadiusUV, uint2 mapSize, out float averageDepth)
{
    float totalDepth = 0.0f;
    uint blockerCount = 0;
    [branch]
    if (BlockerSearchSampleCount == 25u)
    {
        // Preserve the original 25 offsets exactly for A/B comparisons.
        [unroll]
        for (uint index = 0; index < 25; ++index)
            AccumulateBlockerDepth(uv + ShadowDisk[index] * searchRadiusUV,
                receiverDepth, bias, mapSize, totalDepth, blockerCount);
    }
    else
    {
        // A center tap catches small blockers; the remaining taps fill the
        // same disk without repeating the 25-point pattern.
        const uint sampleCount = min(max(BlockerSearchSampleCount, 1u), 128u);
        const float2 rotation = float2(-0.7373688781f, 0.6754902943f);
        float2 direction = float2(1.0f, 0.0f);
        [loop]
        for (uint index = 0; index < sampleCount; ++index)
        {
            float2 offset = 0.0f;
            if (index > 0u)
            {
                float radius = sqrt(max(float(index) - 0.5f, 0.0f)
                    / float(max(sampleCount - 1u, 1u)));
                offset = direction * radius;
                direction = float2(direction.x * rotation.x - direction.y * rotation.y,
                    direction.x * rotation.y + direction.y * rotation.x);
            }
            AccumulateBlockerDepth(uv + offset * searchRadiusUV,
                receiverDepth, bias, mapSize, totalDepth, blockerCount);
        }
    }
    averageDepth = totalDepth / float(max(blockerCount, 1u));
    return blockerCount > 0;
}

bool FindPcssFilterRadiusUV(float2 uv, float receiverDepth, ShadowViewGpu shadow,
    uint2 mapSize, out float2 filterRadiusUV)
{
    float2 texelSize = rcp(float2(mapSize));
    float2 orthoSize = max(ShadowOrthoSize, float2(1e-6f, 1e-6f));
    float2 searchRadiusUV = GetPcssBlockerSearchRadiusUV(mapSize);
    float averageBlockerDepth;
    if (!FindAverageBlockerDepth(uv, receiverDepth, shadow.DepthBias,
        searchRadiusUV, mapSize, averageBlockerDepth))
    {
        filterRadiusUV = 0.0f;
        return false;
    }

    // Orthographic D3D depth is linear. Remove the receiver comparison bias
    // from the gap so contact points do not acquire a spurious penumbra.
    float depthDifferenceWorld = max(receiverDepth - averageBlockerDepth - shadow.DepthBias, 0.0f)
        * max(shadow.FarPlane - shadow.NearPlane, 0.0f);
    float penumbraRadiusWorld = depthDifferenceWorld * TanLightAngularRadius;
    float filterRadiusWorld = clamp(penumbraRadiusWorld,
        max(MinFilterRadiusWorld, 0.0f), max(MaxFilterRadiusWorld, MinFilterRadiusWorld));
    filterRadiusUV = max(filterRadiusWorld / orthoSize, texelSize);
    return true;
}

float2 GetPcssTapRadiusUV(float2 filterRadiusUV, uint2 mapSize)
{
    // A final comparison tap must not reach beyond the area where a blocker
    // could be found; this also holds when the runtime spacing changes.
    return min(filterRadiusUV * PcssPcfTapSpacingScale,
        GetPcssBlockerSearchRadiusUV(mapSize));
}

float FilterShadowPcss(float2 uv, float receiverDepth, ShadowViewGpu shadow)
{
    uint shadowWidth, shadowHeight;
    DirectionalShadowMap.GetDimensions(shadowWidth, shadowHeight);
    float2 filterRadiusUV;
    if (!FindPcssFilterRadiusUV(uv, receiverDepth, shadow,
        uint2(shadowWidth, shadowHeight), filterRadiusUV)) return 1.0f;
    // Density experiment: keep blocker search and the calculated
    // penumbra fixed; change only the final comparison tap footprint.
    float2 tapRadiusUV = GetPcssTapRadiusUV(filterRadiusUV, uint2(shadowWidth, shadowHeight));

    float visibility = 0.0f;
    if (ShadowFilterMode == 3)
    {
        // Exact pre-existing 25-tap pattern for A/B comparison.
        [unroll]
        for (uint index = 0; index < 25; ++index)
            visibility += DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler,
                uv + ShadowDisk[index] * tapRadiusUV, receiverDepth - shadow.DepthBias);
        return saturate(visibility / 25.0f);
    }

    // Vogel disk: each supported count fills the whole disk, with a fixed angle
    // sequence. Iterative rotation avoids per-tap trigonometry and frame noise.
    const uint sampleCount = min(max(PcssPcfSampleCount, 1u), 128u);
    const float inverseCount = rcp(float(sampleCount));
    const float2 rotation = float2(-0.7373688781f, 0.6754902943f); // Golden angle.
    float2 direction = float2(1.0f, 0.0f);
    [loop]
    for (uint index = 0; index < sampleCount; ++index)
    {
        float radius = sqrt((float(index) + 0.5f) * inverseCount);
        visibility += DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler,
            uv + direction * radius * tapRadiusUV, receiverDepth - shadow.DepthBias);
        direction = float2(direction.x * rotation.x - direction.y * rotation.y,
            direction.x * rotation.y + direction.y * rotation.x);
    }
    return saturate(visibility * inverseCount);
}

#endif
