#pragma once

#include <cstdint>

namespace Canape
{
    // Renderer registration reference, not a CPU asset ID. Zero is invalid;
    // nonzero handles may become stale after explicit removal or renderer reset.
    struct TextureHandle
    {
        uint64_t Value = 0;

        constexpr bool IsValid() const { return Value != 0; }
        constexpr bool operator==(const TextureHandle&) const = default;
    };
}
