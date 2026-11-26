#include "Common/MeshTransform.hlsli"

VertexOutput VSMain(StaticVertexInput input)
{
    LocalSurface surface = ReadSurface(input.Position, input.Normal, input.UV, input.Tangent, input.UV1);
    return BuildVertexOutput(surface);
}
