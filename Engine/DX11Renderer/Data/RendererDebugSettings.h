#pragma once

#include <cstdint>

namespace Canape
{
    enum class DebugViewMode : uint8_t
    {
        Final = 0,
        CameraDepth = 1,
        WorldNormal = 2,
        UV = 3,
        MipLOD = 4,
        ShadowDepth = 5,
        ShadowVisibility = 6,
        PcssFilterRadius = 7
    };

    enum class ShadowCullMode : uint8_t
    {
        None,
        Front,
        Back
    };

    constexpr float DefaultShadowSlopeBias = 2.0f;
    constexpr float DefaultShadowReceiverBias = 0.0003f;

    struct RendererDebugSettings
    {
        bool enabled = false;
        DebugViewMode viewMode = DebugViewMode::Final;
        // Range in the raw, non-linear D3D depth buffer (0 = near, 1 = far).
        float depthMin = 0.95f;
        float depthMax = 1.0f;
        bool wireframe = false;
        bool captureGpuTimings = false;
        ShadowCullMode shadowCullMode = ShadowCullMode::Back;
        float shadowSlopeBias = DefaultShadowSlopeBias;
        float shadowReceiverBias = DefaultShadowReceiverBias;
        bool shadowPcf = true;
    };
}
