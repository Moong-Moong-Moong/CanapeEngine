#ifndef CANAPE_COMMON_MESHSURFACE_HLSLI
#define CANAPE_COMMON_MESHSURFACE_HLSLI

#include "MeshTypes.hlsli"
#include "Material.hlsli"

float3 GetWorldNormal(VertexOutput input)
{
    float3 geometricNormal = cross(ddy(input.WorldPosition), ddx(input.WorldPosition));
    float3 normal = dot(input.WorldNormal, input.WorldNormal) > 1e-12f
        ? input.WorldNormal : geometricNormal;
    return normal * rsqrt(max(dot(normal, normal), 1e-20f));
}

// Shared by final shading and WorldNormal debug so both display the same surface.
float3 GetShadingNormal(VertexOutput input)
{
    float3 normal = GetWorldNormal(input);
    if (HasNormalTexture == 0) return normal;

    float3 tangent = input.Tangent.xyz - normal * dot(normal, input.Tangent.xyz);
    float tangentLengthSquared = dot(tangent, tangent);
    if (tangentLengthSquared > 1e-12f)
    {
        tangent *= rsqrt(tangentLengthSquared);
        float handedness = input.Tangent.w < 0.0f ? -1.0f : 1.0f;
        float3 bitangent = cross(normal, tangent) * handedness;
        float3 tangentNormal = NormalTexture.Sample(LinearSampler, input.UV).xyz * 2.0f - 1.0f;
        float3 mappedNormal = mul(tangentNormal, float3x3(tangent, bitangent, normal));
        float mappedLengthSquared = dot(mappedNormal, mappedNormal);
        if (mappedLengthSquared > 1e-12f)
            normal = mappedNormal * rsqrt(mappedLengthSquared);
    }
    return normal;
}

#endif
