#pragma once

#include <cstdint>

namespace Canape
{
    enum class EditorGridPlane : uint8_t
    {
        XZ = 0,
        XY = 1,
        ZY = 2
    };

    inline constexpr uint32_t EditorGridPlaneCount = 3;
}
