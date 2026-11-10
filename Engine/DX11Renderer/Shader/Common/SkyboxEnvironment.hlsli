#ifndef CANAPE_COMMON_SKYBOX_ENVIRONMENT_HLSLI
#define CANAPE_COMMON_SKYBOX_ENVIRONMENT_HLSLI

// Shared by the skybox pass and mesh specular IBL. Matches SkyboxViewConstants.
cbuffer SkyboxViewBuffer : register(b6)
{
    row_major float4x4 InverseProjection;
    row_major float4x4 InverseView;
    float Brightness;
    float RotationCos;
    float RotationSin;
    float MipCount;
};

TextureCube SkyboxTexture : register(t2);
SamplerState SkyboxSampler : register(s2);

float3 RotateIntoSkybox(float3 worldDirection)
{
    // Inverse yaw maps world directions into the rotated cubemap's coordinates.
    return float3(
        RotationCos * worldDirection.x - RotationSin * worldDirection.z,
        worldDirection.y,
        RotationSin * worldDirection.x + RotationCos * worldDirection.z);
}

#endif
