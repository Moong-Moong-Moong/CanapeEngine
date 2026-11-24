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
        // Validated scene submesh draws before pass expansion; one mesh instance can contribute several items.
        uint32_t renderItems = 0;
        uint32_t pointLights = 0; // Accepted local Point lights uploaded for the scene.
        uint32_t sceneSamples = 1; // Actual MSAA sample count after device fallback.

        // Latest completed GPU query frame; may lag behind frameId.
        uint64_t gpuFrameId = 0;
        bool gpuTimingValid = false;
        double gpuFrameMs = 0.0;
        double drawSceneMs = 0.0;
        double finalOutputMs = 0.0;
    };
}
