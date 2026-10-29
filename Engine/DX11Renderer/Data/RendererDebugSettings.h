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
        MipLOD = 4
    };

    struct RendererDebugSettings
    {
        bool enabled = false;
        DebugViewMode viewMode = DebugViewMode::Final;
        // Range in the raw, non-linear D3D depth buffer (0 = near, 1 = far).
        float depthMin = 0.95f;
        float depthMax = 1.0f;
        bool wireframe = false;
        bool captureGpuTimings = false;
    };
}
