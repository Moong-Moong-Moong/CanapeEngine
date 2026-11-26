#ifndef CANAPE_COMMON_LIGHTING_HLSLI
#define CANAPE_COMMON_LIGHTING_HLSLI

#include "Constants.hlsli"
#include "SkyboxEnvironment.hlsli"
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
        if (distanceSquared >= light.Range * light.Range) continue;
        float distanceToLight = sqrt(distanceSquared);
        
        // Finite at the light center and smoothly zero at the range boundary.
        float3 lightDirection = toLight * rsqrt(max(distanceSquared, 1e-12f));
        float attenuation = saturate(1.0f - distanceToLight / light.Range);
        attenuation *= attenuation;
        lighting += light.ColorIntensity * saturate(dot(normal, lightDirection)) * attenuation;
    }
    return lighting;
}

static const float PBR_PI = 3.14159265359f;

float DistributionGGX(float nDotH, float roughness)
{
    // GGX alpha is perceptual roughness squared; alpha squared enters D.
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float term = nDotH * nDotH * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(PBR_PI * term * term, 1e-7f);
}

float GeometrySchlickGGX(float nDotX, float roughness)
{
    // Direct-lighting approximation (different from the IBL geometry term).
    float k = (roughness + 1.0f) * (roughness + 1.0f) * 0.125f;
    return nDotX / max(nDotX * (1.0f - k) + k, 1e-7f);
}

float GeometrySmith(float nDotV, float nDotL, float roughness)
{
    return GeometrySchlickGGX(nDotV, roughness) * GeometrySchlickGGX(nDotL, roughness);
}

float3 FresnelSchlick(float hDotV, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - hDotV, 5.0f);
}

// Rough surfaces retain some grazing reflectance in the diffuse energy split.
float3 FresnelSchlickRoughness(float nDotV, float3 f0, float roughness)
{
    return f0 + (max(1.0f - roughness, f0) - f0) * pow(1.0f - nDotV, 5.0f);
}

float3 EvaluateCookTorranceBRDF(float3 baseColor, float metallic, float roughness,
    float3 normal, float3 viewDirection, float3 lightDirection)
{
    float nDotL = saturate(dot(normal, lightDirection));
    float nDotV = saturate(dot(normal, viewDirection));
    if (nDotL <= 0.0f || nDotV <= 0.0f) return 0.0f;

    float3 halfVector = viewDirection + lightDirection;
    float halfLengthSquared = dot(halfVector, halfVector);
    if (halfLengthSquared <= 1e-12f) return 0.0f;
    halfVector *= rsqrt(halfLengthSquared);

    metallic = saturate(metallic);
    roughness = clamp(roughness, 0.045f, 1.0f);
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
    float3 fresnel = FresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    float distribution = DistributionGGX(saturate(dot(normal, halfVector)), roughness);
    float geometry = GeometrySmith(nDotV, nDotL, roughness);
    float3 specular = distribution * geometry * fresnel / max(4.0f * nDotV * nDotL, 1e-7f);
    float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * baseColor / PBR_PI;
    return diffuse + specular;
}

float3 EvaluatePBRDirectionalLight(float3 worldPosition, float3 normal, float3 viewDirection,
    float3 baseColor, float metallic, float roughness, DirectionalLightGpu light)
{
    float3 lightDirection = -light.Direction;
    float nDotL = saturate(dot(normal, lightDirection));
    if (nDotL <= 0.0f) return 0.0f;
    float visibility = DirectionalShadowVisibility(worldPosition, light);
    return EvaluateCookTorranceBRDF(baseColor, metallic, roughness, normal, viewDirection, lightDirection)
        * light.ColorIntensity * nDotL * visibility;
}

float3 EvaluatePBRPointLight(float3 worldPosition, float3 normal, float3 viewDirection,
    float3 baseColor, float metallic, float roughness, LocalLightGpu light)
{
    if (light.Type != LOCAL_LIGHT_POINT || light.Range <= 0.0f) return 0.0f;
    float3 toLight = light.Position - worldPosition;
    float distanceSquared = dot(toLight, toLight);
    if (distanceSquared >= light.Range * light.Range) return 0.0f;
    float distanceToLight = sqrt(distanceSquared);
    float3 lightDirection = toLight * rsqrt(max(distanceSquared, 1e-12f));
    float attenuation = saturate(1.0f - distanceToLight / light.Range);
    attenuation *= attenuation;
    return EvaluateCookTorranceBRDF(baseColor, metallic, roughness, normal, viewDirection, lightDirection)
        * light.ColorIntensity * saturate(dot(normal, lightDirection)) * attenuation;
}

float3 CalculatePBRDirectLighting(float3 worldPosition, float3 normal, float3 baseColor,
    float metallic, float roughness)
{
    float3 toCamera = CameraPosition - worldPosition;
    float3 viewDirection = toCamera * rsqrt(max(dot(toCamera, toCamera), 1e-12f));
    float3 lighting = 0.0f;
    [loop]
    for (uint directionalIndex = 0; directionalIndex < min(DirectionalCount, MAX_DIRECTIONAL_LIGHTS); ++directionalIndex)
        lighting += EvaluatePBRDirectionalLight(worldPosition, normal, viewDirection, baseColor,
            metallic, roughness, DirectionalLights[directionalIndex]);
    [loop]
    for (uint localIndex = 0; localIndex < min(LocalCount, MAX_LOCAL_LIGHTS); ++localIndex)
        lighting += EvaluatePBRPointLight(worldPosition, normal, viewDirection, baseColor,
            metallic, roughness, LocalLights[localIndex]);
    return lighting;
}

float3 CalculateSpecularIBL(float3 worldPosition, float3 normal, float3 baseColor,
    float metallic, float roughness)
{
    if (MipCount < 1.0f || Brightness <= 0.0f) return 0.0f;
    float3 toCamera = CameraPosition - worldPosition;
    float3 viewDirection = toCamera * rsqrt(max(dot(toCamera, toCamera), 1e-12f));
    float3 reflection = reflect(-viewDirection, normal);
    float3 sampleDirection = RotateIntoSkybox(reflection);
    float lod = saturate(roughness) * (MipCount - 1.0f);
    float3 radiance = SkyboxTexture.SampleLevel(SkyboxSampler, sampleDirection, lod).rgb * Brightness;
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, saturate(metallic));
    float3 fresnel = FresnelSchlick(saturate(dot(normal, viewDirection)), f0);
    return radiance * fresnel;
}

TextureCube IrradianceMap : register(t6);
TextureCube PrefilterMap : register(t7);
Texture2D BRDFLUT : register(t8);
SamplerState BRDFSampler : register(s3);

cbuffer IBLBuffer : register(b10)
{
    uint IBLAvailable;
    uint PrefilterMipCount;
    uint2 IBLPadding;
};

float3 CalculateDiffuseIBL(float3 normal, float3 baseColor, float metallic,
    float roughness, float nDotV, float3 f0)
{
    float3 fresnel = FresnelSchlickRoughness(nDotV, f0, roughness);
    float3 kD = (1.0f - fresnel) * (1.0f - metallic);
    float3 direction = RotateIntoSkybox(normal);
    // BakeIrradiance stores E(N)/PI, so there is no additional PI factor here.
    return kD * baseColor * IrradianceMap.SampleLevel(SkyboxSampler, direction, 0.0f).rgb;
}

float3 CalculateSplitSumSpecularIBL(float3 normal, float3 viewDirection,
    float roughness, float nDotV, float3 f0)
{
    float3 reflection = reflect(-viewDirection, normal);
    float3 direction = RotateIntoSkybox(reflection);
    float lod = roughness * float(max(PrefilterMipCount, 1u) - 1u);
    float3 radiance = PrefilterMap.SampleLevel(SkyboxSampler, direction, lod).rgb;
    float2 ab = BRDFLUT.SampleLevel(BRDFSampler, float2(nDotV, roughness), 0.0f).rg;
    // The LUT already contains Geometry and Schlick Fresnel's two coefficients.
    return radiance * (f0 * ab.x + ab.y);
}

#endif
