#ifndef CANAPE_COMMON_LIGHTING_HLSLI
#define CANAPE_COMMON_LIGHTING_HLSLI

#include "Constants.hlsli"
#include "../Shadow/ShadowSampling.hlsli"

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

#endif
