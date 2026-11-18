#ifndef CANAPE_COMMON_IBL_SAMPLING_HLSLI
#define CANAPE_COMMON_IBL_SAMPLING_HLSLI

static const float IBL_PI = 3.14159265358979323846f;

// D3D cube face order: +X, -X, +Y, -Y, +Z, -Z. UV is top-left origin.
float3 IBLCubeDirection(uint face, float2 uv)
{
    float2 p = uv * 2.0f - 1.0f;
    if (face == 0) return normalize(float3( 1.0f, -p.y, -p.x));
    if (face == 1) return normalize(float3(-1.0f, -p.y,  p.x));
    if (face == 2) return normalize(float3( p.x,  1.0f,  p.y));
    if (face == 3) return normalize(float3( p.x, -1.0f, -p.y));
    if (face == 4) return normalize(float3( p.x, -p.y,  1.0f));
    return normalize(float3(-p.x, -p.y, -1.0f));
}

float IBLRadicalInverse(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return float(bits) * 2.3283064365386963e-10f;
}

float2 IBLHammersley(uint i, uint count)
{
    return float2(float(i) / float(count), IBLRadicalInverse(i));
}

float IBLSourceMipFromPdf(float pdf, uint sampleCount, float sourceResolution, uint sourceMipCount)
{
    // Callers clamp their PDF above zero; one exposed mip must always sample LOD 0.
    if (sourceMipCount <= 1) return 0.0f;
    float sampleSolidAngle = 1.0f / (float(sampleCount) * pdf);
    float texelSolidAngle = 4.0f * IBL_PI / (6.0f * sourceResolution * sourceResolution);
    return clamp(0.5f * log2(sampleSolidAngle / texelSolidAngle),
        0.0f, float(sourceMipCount - 1));
}

void IBLTangentBasis(float3 n, out float3 tangent, out float3 bitangent)
{
    float3 helper = abs(n.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(helper, n));
    bitangent = cross(n, tangent);
}

float3 IBLImportanceSampleGGX(float2 xi, float3 n, float roughness)
{
    // Same perceptual roughness mapping as Lighting.hlsli: alpha = roughness squared.
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float phi = 2.0f * IBL_PI * xi.x;
    float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (alphaSquared - 1.0f) * xi.y));
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    float3 tangent, bitangent;
    IBLTangentBasis(n, tangent, bitangent);
    return normalize(tangent * (cos(phi) * sinTheta)
        + bitangent * (sin(phi) * sinTheta) + n * cosTheta);
}

#endif
