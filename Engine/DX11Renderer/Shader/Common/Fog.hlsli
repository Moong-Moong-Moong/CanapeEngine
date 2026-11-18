#ifndef CANAPE_COMMON_FOG_HLSLI
#define CANAPE_COMMON_FOG_HLSLI

#include "Constants.hlsli"

// Fog weight: 0 keeps the lit surface color; 1 replaces it with linear FogColor.
// These mode values match RenderScene.h::FogMode.
static const uint FOG_MODE_LINEAR = 1;
static const uint FOG_MODE_EXP = 2;
static const uint FOG_MODE_EXP2 = 3;
static const uint FOG_MODE_HEIGHT = 4;

float CalculateLinearFogWeight(float distanceToCamera)
{
    if (FogEndDistance <= FogStartDistance)
        return distanceToCamera >= FogEndDistance ? 1.0f : 0.0f;
    return saturate((distanceToCamera - FogStartDistance) / (FogEndDistance - FogStartDistance));
}

float CalculateExpFogWeight(float distanceToCamera)
{
    return 1.0f - exp(-max(FogDensity, 0.0f) * distanceToCamera);
}

float CalculateExp2FogWeight(float distanceToCamera)
{
    float opticalDepth = max(FogDensity, 0.0f) * distanceToCamera;
    return 1.0f - exp(-opticalDepth * opticalDepth);
}

float CalculateHeightFogWeight(float3 worldPosition, float distanceToCamera)
{
    // Local density at the pixel height; this is a distance approximation, not ray integration.
    float heightExponent = clamp(-max(FogHeightFalloff, 0.0f)
        * (worldPosition.y - FogBaseHeight), -80.0f, 80.0f);
    float localDensity = max(FogDensity, 0.0f) * exp(heightExponent);
    return 1.0f - exp(-localDensity * distanceToCamera);
}

float CalculateFogWeight(float3 worldPosition)
{
    if (FogMode != FOG_MODE_LINEAR && FogMode != FOG_MODE_EXP
        && FogMode != FOG_MODE_EXP2 && FogMode != FOG_MODE_HEIGHT)
        return 0.0f; // Disabled and unknown modes.

    float distanceToCamera = distance(CameraPosition, worldPosition);
    if (distanceToCamera <= 0.0f) return 0.0f;
    if (FogMode == FOG_MODE_LINEAR)
        return CalculateLinearFogWeight(distanceToCamera);
    if (FogMode == FOG_MODE_EXP)
        return CalculateExpFogWeight(distanceToCamera);
    if (FogMode == FOG_MODE_EXP2)
        return CalculateExp2FogWeight(distanceToCamera);
    return CalculateHeightFogWeight(worldPosition, distanceToCamera);
}

#endif
