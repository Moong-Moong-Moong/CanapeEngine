#ifndef CANAPE_COMMON_MESHTRANSFORM_HLSLI
#define CANAPE_COMMON_MESHTRANSFORM_HLSLI

#include "Constants.hlsli"
#include "MeshTypes.hlsli"

LocalSurface ReadSurface(float3 position, float3 normal, float2 uv, float4 tangent, float2 uv1)
{
    LocalSurface surface;
    surface.Position = position;
    surface.Normal = normal;
    surface.UV = uv;
    surface.Tangent = tangent;
    surface.UV1 = uv1;
    return surface;
}

VertexOutput BuildVertexOutput(LocalSurface surface)
{
    VertexOutput output;
    float4 worldPosition = mul(float4(surface.Position, 1.0f), World);
    output.Position = mul(worldPosition, ViewProjection);
    output.WorldPosition = worldPosition.xyz;
    output.WorldNormal = mul(float4(surface.Normal, 0.0f), WorldInverseTranspose).xyz;
    output.UV = surface.UV;
    output.UV1 = surface.UV1;
    // Keep the existing handedness; MeshSurface orthogonalizes/normalizes the TBN.
    output.Tangent = float4(mul(float4(surface.Tangent.xyz, 0.0f), World).xyz, surface.Tangent.w);
    return output;
}

#endif
