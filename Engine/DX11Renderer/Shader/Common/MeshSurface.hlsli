#ifndef CANAPE_COMMON_MESHSURFACE_HLSLI
#define CANAPE_COMMON_MESHSURFACE_HLSLI

#include "MeshTypes.hlsli"

float3 GetWorldNormal(VertexOutput input)
{
    float3 geometricNormal = cross(ddy(input.WorldPosition), ddx(input.WorldPosition));
    float3 normal = dot(input.WorldNormal, input.WorldNormal) > 1e-12f
        ? input.WorldNormal : geometricNormal;
    return normal * rsqrt(max(dot(normal, normal), 1e-20f));
}

#endif
