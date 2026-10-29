#pragma once

#include <cstdint>

namespace Canape
{
    struct RendererDebugStats
    {
        // Latest completed CPU submission frame. Includes the FinalOutput draw.
        uint64_t frameId = 0;
        uint32_t drawCalls = 0;
        uint64_t triangleCount = 0;

        // Latest completed GPU query frame; may lag behind frameId.
        uint64_t gpuFrameId = 0;
        bool gpuTimingValid = false;
        double gpuFrameMs = 0.0;
        double drawSceneMs = 0.0;
        double finalOutputMs = 0.0;
    };
}
