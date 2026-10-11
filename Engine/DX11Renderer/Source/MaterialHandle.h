#pragma once

#include <cstdint>

namespace Canape
{
    // Placeholder only. No material resources are registered yet; zero is invalid.
    struct MaterialHandle
    {
        uint64_t Value = 0;

        constexpr bool IsValid() const { return Value != 0; }
        constexpr bool operator==(const MaterialHandle&) const = default;
    };
}
