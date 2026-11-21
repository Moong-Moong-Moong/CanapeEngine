#ifndef CANAPE_SKY_HLSLI
#define CANAPE_SKY_HLSLI

#include "Common/Constants.hlsli"

// Where a direction of the sky is on the screen: a point at infinity, which the camera's movement does not shift.
float4 SkyPosition(float3 direction)
{
    float4 position = mul(float4(direction, 0.0), ViewProjection);
    // In front of the cleared depth and behind everything the scene draws.
    position.z = position.w * 0.99999;
    return position;
}

#endif
