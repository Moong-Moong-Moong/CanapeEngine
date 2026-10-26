#pragma once

#include <cstdint>

namespace Canape
{
    struct TextureHandle
    {
        uint64_t Value = 0;

        constexpr bool IsValid() const { return Value != 0; }
        constexpr bool operator==(const TextureHandle&) const = default;
    };
}
