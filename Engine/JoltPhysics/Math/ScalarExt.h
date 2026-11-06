#pragma once

#include <bit>
#include <cmath>
#include <cstdint>

namespace Canape
{
    enum class Easing : std::uint8_t
    {
        Linear,
        Smooth,
    };

    inline constexpr float Ease(Easing easing, float u)
    {
        using enum Easing;

        switch (easing)
        {
        case Smooth:
            return u * u * (3.0f - 2.0f * u);
        default:
            return u;
        }
    }

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

    inline constexpr float Lerp(float lhs, float rhs, float s)
    {
        return lhs + (rhs - lhs) * s;
    }
}