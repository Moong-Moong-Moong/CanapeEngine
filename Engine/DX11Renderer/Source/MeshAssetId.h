#pragma once

#include <cstdint>

namespace Canape
{
    // CPU asset identity issued by Engine. Shared with Renderer; zero is invalid.
    struct MeshAssetId
    {
        uint64_t Value = 0;

        constexpr bool IsValid() const { return Value != 0; }
        constexpr bool operator==(const MeshAssetId&) const = default;
    };
}
