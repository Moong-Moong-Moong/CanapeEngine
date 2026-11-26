#include "Common/Constants.hlsli"
#include "Common/MeshTypes.hlsli"
#include "Common/MeshSurface.hlsli"
#include "Common/Material.hlsli"
#include "Common/Lighting.hlsli"
#include "Common/Fog.hlsli"

float4 PSMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    float3 normal = GetShadingNormal(input, isFrontFace);
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
    float3 ambient;
    if (IBLAvailable != 0)
    {
        float safeMetallic = saturate(metallic);
        float safeRoughness = saturate(roughness);
        float3 toCamera = CameraPosition - input.WorldPosition;
        float distanceSquared = dot(toCamera, toCamera);
        float3 viewDirection = distanceSquared > 1e-12f
            ? toCamera * rsqrt(distanceSquared) : normal;
        float nDotV = saturate(dot(normal, viewDirection));
        float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, safeMetallic);
        float3 diffuseIBL = CalculateDiffuseIBL(normal, baseColor, safeMetallic,
            safeRoughness, nDotV, f0) * occlusion;
        float3 specularIBL = CalculateSplitSumSpecularIBL(normal, viewDirection,
            safeRoughness, nDotV, f0);
        // The baked maps contain unscaled HDR radiance. Apply scene brightness once.
        ambient = (diffuseIBL + specularIBL) * Brightness;
        
        // Diffuse IBL만 보기
        //return float4(diffuseIBL * Brightness, 1.0f);

        // Specular IBL만 보기
        //return float4(specularIBL * Brightness, 1.0f);
    }
    else
    {
        // Keep the legacy ambient and skybox reflection when any IBL resource is missing.
        ambient = AmbientColor * AmbientIntensity * baseColor * occlusion
            + CalculateSpecularIBL(input.WorldPosition, normal, baseColor, metallic, roughness);
    }
    float3 rgb = CalculatePBRDirectLighting(input.WorldPosition, normal, baseColor, metallic, roughness)
        + ambient;
    if (HasLightmap != 0)
    {
        // Linear, albedo-free indirect irradiance / PI. Use the same diffuse
        // energy split as IBL; metals contribute no diffuse reflection.
        float safeMetallic = saturate(metallic);
        float3 toCamera = CameraPosition - input.WorldPosition;
        float3 viewDirection = dot(toCamera, toCamera) > 1e-12f ? normalize(toCamera) : normal;
        float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, safeMetallic);
        float3 fresnel = FresnelSchlickRoughness(saturate(dot(normal, viewDirection)), f0, saturate(roughness));
        float3 diffuseReflectance = (1.0f - fresnel) * (1.0f - safeMetallic) * baseColor;
        float3 bakedLight = LightmapTexture.Sample(LinearSampler, input.UV1).rgb;
        // Preserve realtime lighting/IBL. Baked occlusion and brightness are already in the map.
        rgb += diffuseReflectance * bakedLight;
    }
    rgb = lerp(rgb, FogColor, CalculateFogWeight(input.WorldPosition));
    return float4(rgb, MaterialAlphaMode == 2 ? alpha : 1.0f);
}
