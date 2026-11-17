// Compatibility entry points for the existing C++ compile paths.
// STATIC_VERTEX selects full Static input; absence means PositionOnly, not Skinned.
#ifdef STATIC_VERTEX
#include "MeshStaticVS.hlsl"
#else
#include "Common/MeshTransform.hlsli"

VertexOutput VSMain(PositionVertexInput input)
{
    LocalSurface surface = ReadSurface(input.Position, float3(0, 0, 0),
        float2(0, 0), float4(0, 0, 0, 1));
    return BuildVertexOutput(surface);
}
#endif

#include "MeshPS.hlsl"
