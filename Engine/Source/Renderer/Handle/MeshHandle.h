#pragma once

#include <cstdint>

namespace Canape
{
    struct MeshHandle
    {
        uint64_t Value = 0;

        constexpr bool IsValid() const { return Value != 0; }
        constexpr bool operator==(const MeshHandle&) const = default;
    };
}
