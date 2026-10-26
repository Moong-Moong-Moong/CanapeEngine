#pragma once

#include <cstdint>

namespace Canape
{
    struct MaterialHandle
    {
        uint64_t Value = 0;

        constexpr bool IsValid() const { return Value != 0; }
        constexpr bool operator==(const MaterialHandle&) const = default;
    };
}
