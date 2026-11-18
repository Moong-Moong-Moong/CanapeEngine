#include "Common/Constants.hlsli"
#include "Common/MeshTypes.hlsli"
#include "Common/MeshSurface.hlsli"
#include "Common/Material.hlsli"
#include "Common/Lighting.hlsli"
#include "Common/Fog.hlsli"

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 normal = GetShadingNormal(input);
    // sRGB sampling decodes RGB; the FP16 SceneColor target preserves linear HDR output.
    float4 textureColor = BaseColorTexture.Sample(LinearSampler, input.UV);
    float alpha = BaseColor.a * textureColor.a;
    if (MaterialAlphaMode == 1) clip(alpha - AlphaCutoff);
    float3 baseColor = BaseColor.rgb * textureColor.rgb;
    float metallic = MaterialMetallic;
    float roughness = MaterialRoughness;
    if (HasORMTexture != 0)
    {
        float3 orm = ORMTexture.Sample(LinearSampler, input.UV).rgb;
        roughness *= orm.g;
        metallic *= orm.b;
    }
    // AO is a separate material input: metallicRoughness images need not contain AO in R.
    float occlusion = 1.0f;
    if (HasOcclusionTexture != 0)
        occlusion = OcclusionTexture.Sample(LinearSampler, input.UV).r;
    // Apply AO to ambient diffuse only; direct lighting and specular IBL stay independent.
    float3 rgb = AmbientColor * AmbientIntensity * baseColor * occlusion
        + CalculatePBRDirectLighting(input.WorldPosition, normal, baseColor, metallic, roughness)
        + CalculateSpecularIBL(input.WorldPosition, normal, baseColor, metallic, roughness);
    rgb = lerp(rgb, FogColor, CalculateFogWeight(input.WorldPosition));
    return float4(rgb, MaterialAlphaMode == 2 ? alpha : 1.0f);
}
