#include "Common/Constants.hlsli"

Texture2D BaseColorTexture : register(t0);
SamplerState LinearSampler : register(s0);
Texture2D<float> DirectionalShadowMap : register(t1);
SamplerComparisonState ShadowSampler : register(s1);

// DX11Renderer-local settings. ShadowBuffer b5 and its public layout are unchanged.
cbuffer PcssBuffer : register(b7)
{
    float2 ShadowOrthoSize;
    float TanLightAngularRadius;
    float BlockerSearchRadiusWorld;
    float MinFilterRadiusWorld;
    float MaxFilterRadiusWorld;
    uint ShadowFilterMode;
    uint PcssPcfSampleCount;
    float PcssPcfTapSpacingScale;
    uint BlockerSearchSampleCount;
    float2 PcssPadding;
};

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

struct VertexInput
{
    float3 Position : POSITION;
#ifdef STATIC_VERTEX
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
#endif
};

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : WORLDPOS;
    float3 WorldNormal : NORMAL;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    output.Position = mul(worldPosition, ViewProjection);
    output.WorldPosition = worldPosition.xyz;
#ifdef STATIC_VERTEX
    output.WorldNormal = mul(float4(input.Normal, 0.0f), WorldInverseTranspose).xyz;
    output.UV = input.UV;
#else
    output.WorldNormal = float3(0, 0, 0);
    output.UV = float2(0, 0);
#endif
    return output;
}

struct ShadowVertexOutput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

struct ShadowVertexInput
{
    float3 Position : POSITION;
#ifdef STATIC_VERTEX
    float2 UV : TEXCOORD0;
#endif
};

ShadowVertexOutput VSShadow(ShadowVertexInput input)
{
    ShadowVertexOutput output;
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    output.Position = mul(worldPosition, ShadowViews[0].LightViewProjection);
#ifdef STATIC_VERTEX
    output.UV = input.UV;
#else
    output.UV = 0.0f;
#endif
    return output;
}

void PSShadowMasked(ShadowVertexOutput input)
{
    // Same alpha texture, base alpha and cutoff as the main masked-material shader.
    float alpha = BaseColor.a * BaseColorTexture.Sample(LinearSampler, input.UV).a;
    clip(alpha - AlphaCutoff);
}

float DirectionalShadowVisibility(float3 worldPosition, DirectionalLightGpu light)
{
    if (light.ShadowViewCount == 0 || light.ShadowViewOffset >= min(ShadowViewCount, MAX_SHADOW_VIEWS))
        return 1.0f;
    ShadowViewGpu shadow = ShadowViews[light.ShadowViewOffset];
    if (shadow.MapIndex != 0) return 1.0f;
    float4 clipPosition = mul(float4(worldPosition, 1.0f), shadow.LightViewProjection);
    if (clipPosition.w <= 0.0f) return 1.0f;
    float3 ndc = clipPosition.xyz / clipPosition.w;
    if (any(ndc.xy < -1.0f) || any(ndc.xy > 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f)
        return 1.0f;
    // D3D NDC z is [0,1]; texture V points down, unlike NDC Y.
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float compareDepth = ndc.z - shadow.DepthBias;
    if (ShadowPcfEnabled == 0)
        return DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, uv, compareDepth);
    if (ShadowFilterMode == 2 || ShadowFilterMode == 3)
        return FilterShadowPcss(uv, ndc.z, shadow);

    uint shadowWidth, shadowHeight;
    DirectionalShadowMap.GetDimensions(shadowWidth, shadowHeight);
    float2 texelSize = 1.0f / float2(shadowWidth, shadowHeight);
    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
            visibility += DirectionalShadowMap.SampleCmpLevelZero(
                ShadowSampler, uv + float2(x, y) * texelSize, compareDepth);
    }
    return visibility / 9.0f;
}

float3 GetWorldNormal(VertexOutput input)
{
    float3 geometricNormal = cross(ddy(input.WorldPosition), ddx(input.WorldPosition));
    float3 normal = dot(input.WorldNormal, input.WorldNormal) > 1e-12f
        ? input.WorldNormal : geometricNormal;
    return normal * rsqrt(max(dot(normal, normal), 1e-20f));
}

float GetDebugAlpha(VertexOutput input)
{
    if (MaterialAlphaMode == 0) return 1.0f;
    // Preserve masked cutouts and transparent blending without using texture RGB.
    float alpha = BaseColor.a * BaseColorTexture.Sample(LinearSampler, input.UV).a;
    if (MaterialAlphaMode == 1) clip(alpha - AlphaCutoff);
    return MaterialAlphaMode == 2 ? alpha : 1.0f;
}

float3 CalculateDirectLighting(float3 worldPosition, float3 normal)
{
    float3 lighting = 0.0f;
    // A zero count executes no direct-light work; clamp to the GPU array capacities.
    [loop]
    for (uint directionalIndex = 0; directionalIndex < min(DirectionalCount, MAX_DIRECTIONAL_LIGHTS); ++directionalIndex)
    {
        DirectionalLightGpu light = DirectionalLights[directionalIndex];
        // Direction stores world travel direction, so surface-to-light is its opposite.
        float visibility = DirectionalShadowVisibility(worldPosition, light);
        lighting += light.ColorIntensity * saturate(dot(normal, -light.Direction)) * visibility;
    }
    [loop]
    for (uint localIndex = 0; localIndex < min(LocalCount, MAX_LOCAL_LIGHTS); ++localIndex)
    {
        LocalLightGpu light = LocalLights[localIndex];
        if (light.Type != LOCAL_LIGHT_POINT || light.Range <= 0.0f) continue;
        float3 toLight = light.Position - worldPosition;
        float distanceSquared = dot(toLight, toLight);
        float distanceToLight = sqrt(distanceSquared);
        if (distanceToLight >= light.Range) continue;
        // Finite at the light center and smoothly zero at the range boundary.
        float3 lightDirection = toLight * rsqrt(max(distanceSquared, 1e-12f));
        float attenuation = saturate(1.0f - distanceToLight / light.Range);
        attenuation *= attenuation;
        lighting += light.ColorIntensity * saturate(dot(normal, lightDirection)) * attenuation;
    }
    return lighting;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 normal = GetWorldNormal(input);
    float3 lighting = AmbientColor * AmbientIntensity
        + CalculateDirectLighting(input.WorldPosition, normal);
    // sRGB sampling decodes RGB; the sRGB RTV encodes the linear output once.
    float4 textureColor = BaseColorTexture.Sample(LinearSampler, input.UV);
    float alpha = BaseColor.a * textureColor.a;
    if (MaterialAlphaMode == 1) clip(alpha - AlphaCutoff);
    float3 rgb = lighting * BaseColor.rgb * textureColor.rgb;
    return float4(rgb, MaterialAlphaMode == 2 ? alpha : 1.0f);
}

float4 PSDebugShadowVisibility(VertexOutput input) : SV_TARGET
{
    float visibility = 1.0f;
    [loop]
    for (uint index = 0; index < min(DirectionalCount, MAX_DIRECTIONAL_LIGHTS); ++index)
    {
        if (DirectionalLights[index].ShadowViewCount != 0)
        {
            visibility = DirectionalShadowVisibility(input.WorldPosition, DirectionalLights[index]);
            break;
        }
    }
    return float4(visibility.xxx, GetDebugAlpha(input));
}

float4 PSDebugPcssFilterRadius(VertexOutput input) : SV_TARGET
{
    float radiusTexels = 0.0f;
    [loop]
    for (uint index = 0; index < min(DirectionalCount, MAX_DIRECTIONAL_LIGHTS); ++index)
    {
        DirectionalLightGpu light = DirectionalLights[index];
        if (light.ShadowViewCount == 0 || light.ShadowViewOffset >= min(ShadowViewCount, MAX_SHADOW_VIEWS))
            continue;
        ShadowViewGpu shadow = ShadowViews[light.ShadowViewOffset];
        if (shadow.MapIndex != 0) continue;
        float4 clipPosition = mul(float4(input.WorldPosition, 1.0f), shadow.LightViewProjection);
        if (clipPosition.w <= 0.0f) break;
        float3 ndc = clipPosition.xyz / clipPosition.w;
        if (any(ndc.xy < -1.0f) || any(ndc.xy > 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f) break;
        float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
        uint width, height;
        DirectionalShadowMap.GetDimensions(width, height);
        float2 filterRadiusUV;
        if (FindPcssFilterRadiusUV(uv, ndc.z, shadow, uint2(width, height), filterRadiusUV))
        {
            float2 tapRadiusUV = GetPcssTapRadiusUV(filterRadiusUV, uint2(width, height));
            radiusTexels = max(tapRadiusUV.x * width, tapRadiusUV.y * height);
        }
        break;
    }
    // Log scale: black = 0, gray levels step at 1, 3, 7, ... texels;
    // white = 1023 or more texels. No blocker is black.
    float gray = saturate(log2(1.0f + radiusTexels) / 10.0f);
    return float4(gray.xxx, GetDebugAlpha(input));
}

float4 PSDebugWorldNormal(VertexOutput input) : SV_TARGET
{
    float3 normal = GetWorldNormal(input);
    return float4(normal * 0.5f + 0.5f, GetDebugAlpha(input));
}

float4 PSDebugUV(VertexOutput input) : SV_TARGET
{
    return float4(frac(input.UV), 0.0f, GetDebugAlpha(input));
}

float4 PSDebugMipLOD(VertexOutput input) : SV_TARGET
{
    // White marks the renderer's fallback texture (no valid material texture).
    if (HasBaseColorTexture == 0) return float4(1.0f, 1.0f, 1.0f, GetDebugAlpha(input));

    uint width, height, mipCount;
    BaseColorTexture.GetDimensions(0, width, height, mipCount);
    float lod = BaseColorTexture.CalculateLevelOfDetail(LinearSampler, input.UV);
    lod = clamp(lod, 0.0f, float(max(mipCount, 1u) - 1u));

    // Distinct integer levels rotate around the color wheel; fractional LOD changes smoothly.
    float3 hue = frac(lod * 0.381966f + float3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f));
    float3 color = saturate(abs(hue * 6.0f - 3.0f) - 1.0f);
    // This reports one LOD value, not every mip touched by trilinear filtering.
    return float4(color, GetDebugAlpha(input));
}
