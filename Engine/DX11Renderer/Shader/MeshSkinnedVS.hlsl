#include "Common/MeshTransform.hlsli"
#include "Common/Skinning.hlsli"

VertexOutput VSMain(SkinnedVertexInput input)
{
    LocalSurface surface = ReadSurface(input.Position, input.Normal, input.UV, input.Tangent, input.UV1);
    surface = ApplySkinning(surface, input.BoneIndices, input.BoneWeights);
    return BuildVertexOutput(surface);
}
