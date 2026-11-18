#pragma once

#include <cstdint>

namespace Canape
{
    // Registers of what a kernel reads and writes.
    inline constexpr std::uint32_t ComputeOuterSlot = 9; // b9: what the world decides, where draws read it too.
    inline constexpr std::uint32_t KernelSlot = 1; // b1: how it is computed.
    inline constexpr std::uint32_t FieldLatestSlot = 0; // t0: the field as it was.
    inline constexpr std::uint32_t FieldNextSlot = 0; // u0: the field being written.
    inline constexpr std::uint32_t ComputeParticlesSlot = 0; // u0: the particles, rewritten in place.

    class ComputePass
    {
    };
}
