#pragma once

#include <cstdint>

namespace Canape
{
    // A default handle names nothing: generations start at 1 and skip 0 when they wrap.
    template<typename T>
    struct SlotHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        constexpr bool operator==(const SlotHandle&) const = default;
    };
}
