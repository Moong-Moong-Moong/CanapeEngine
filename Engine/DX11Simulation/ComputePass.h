#pragma once

#include <cstdint>

namespace Canape
{
    // Registers of what a kernel reads and writes.
    inline constexpr std::uint32_t ComputeOuterSlot = 0; // b0: what the world decides.
    inline constexpr std::uint32_t KernelSlot = 1; // b1: how it is computed.
    inline constexpr std::uint32_t FieldLatestSlot = 0; // t0: the field as it was.
    inline constexpr std::uint32_t FieldNextSlot = 0; // u0: the field being written.

    class ComputePass
    {
    };
}
