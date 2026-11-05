#pragma once

#include <bit>
#include <cmath>
#include <cstdint>

namespace Canape
{
    inline bool IsPositive(float value)
    {
        return std::isfinite(value) && value > 0.0f;
    }

    inline bool IsNonNegative(float value)
    {
        return std::isfinite(value) && value >= 0.0f;
    }

	inline constexpr bool SameBits(float a, float b)
	{
		return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
	}
}