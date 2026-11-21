#pragma once

#include <cstdint>

namespace Canape
{
    // Stands for no frame.
    inline constexpr std::uint64_t NoFrame = UINT64_MAX;

    // The commands a frame issued.
    struct CommandCounts
    {
        // The frame the counts are of, counted from 0; NoFrame before the first has ended.
        std::uint64_t frame = NoFrame;

        std::uint32_t dispatches = 0;
        std::uint32_t draws = 0;
    };

    // The GPU time a frame's passes took.
    struct GpuTiming
    {
        // The frame the times are of; NoFrame while none was measured.
        std::uint64_t frame = NoFrame;

        double computeMs = 0.0;
        double renderMs = 0.0;
    };
}
