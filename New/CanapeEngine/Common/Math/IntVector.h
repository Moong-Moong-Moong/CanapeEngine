#pragma once

#include <cstdint>

namespace Canape
{
	struct IntVector2
	{
		int32_t X = 0;
		int32_t Y = 0;

		constexpr IntVector2() = default;
		constexpr IntVector2(int32_t x, int32_t y) : X(x), Y(y) {}

		static const IntVector2 Zero;

		constexpr IntVector2& operator+=(const IntVector2& rhs) { X += rhs.X; Y += rhs.Y; return *this; }
	};

	inline constexpr IntVector2 IntVector2::Zero{ 0, 0 };
}
