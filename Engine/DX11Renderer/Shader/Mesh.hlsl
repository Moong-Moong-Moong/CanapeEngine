#include "Common/Constants.hlsli"
#include "Common/MeshTypes.hlsli"
#include "Common/MeshSurface.hlsli"
#include "Common/Material.hlsli"
#include "Common/Lighting.hlsli"

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    output.Position = mul(worldPosition, ViewProjection);
    output.WorldPosition = worldPosition.xyz;
#ifdef STATIC_VERTEX
    output.WorldNormal = mul(float4(input.Normal, 0.0f), WorldInverseTranspose).xyz;
    output.UV = input.UV;
    output.Tangent = float4(mul(float4(input.Tangent.xyz, 0.0f), World).xyz, input.Tangent.w);
#else
    output.WorldNormal = float3(0, 0, 0);
    output.UV = float2(0, 0);
    output.Tangent = float4(0, 0, 0, 1);
#endif
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 normal = GetWorldNormal(input);
    float3 tangent = input.Tangent.xyz - normal * dot(normal, input.Tangent.xyz);
    float tangentLengthSquared = dot(tangent, tangent);
    if (tangentLengthSquared > 1e-12f)
    {
        tangent *= rsqrt(tangentLengthSquared);
        float handedness = input.Tangent.w < 0.0f ? -1.0f : 1.0f;
        float3 bitangent = cross(normal, tangent) * handedness;
        float3 tangentNormal = TestNormalTexture.Sample(LinearSampler, input.UV).xyz * 2.0f - 1.0f;
        float3x3 tangentToWorld = float3x3(tangent, bitangent, normal);
        normal = normalize(mul(tangentNormal, tangentToWorld));
    }
    float3 lighting = AmbientColor * AmbientIntensity
        + CalculateDirectLighting(input.WorldPosition, normal);
    // sRGB sampling decodes RGB; the FP16 SceneColor target preserves linear HDR output.
    float4 textureColor = BaseColorTexture.Sample(LinearSampler, input.UV);
    float alpha = BaseColor.a * textureColor.a;
    if (MaterialAlphaMode == 1) clip(alpha - AlphaCutoff);
    float3 rgb = lighting * BaseColor.rgb * textureColor.rgb;
    return float4(rgb, MaterialAlphaMode == 2 ? alpha : 1.0f);
}
