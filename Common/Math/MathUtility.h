#pragma once

#include <cmath>

namespace Canape::Math
{
	inline constexpr float Pi = 3.14159265358979323846f;
	inline constexpr float DegToRad = Pi / 180.0f;
	inline constexpr float RadToDeg = 180.0f / Pi;

	inline constexpr float Epsilon = 1.0e-6f;
	inline constexpr float SmallNumber = 1.0e-4f;

	template<typename T>
	constexpr T Min(T a, T b) { return a < b ? a : b; }

	template<typename T>
	constexpr T Max(T a, T b) { return a > b ? a : b; }

	template<typename T>
	constexpr T Clamp(T value, T min, T max) { return value < min ? min : (value > max ? max : value); }

	template<typename T>
	constexpr T Abs(T value) { return value < T(0) ? -value : value; }

	constexpr float ToRadians(float degrees) { return degrees * DegToRad; }
	constexpr float ToDegrees(float radians) { return radians * RadToDeg; }

	inline float Wrap(float value, float min, float max)
	{
		const float range = max - min;
		if (range <= 0.0f)
		{
			return min;
		}

		float result = std::fmod(value - min, range);
		if (result < 0.0f)
		{
			result += range;
		}
		return result + min;
	}

	inline float WrapDegrees(float degrees)
	{
		return Wrap(degrees, -180.0f, 180.0f);
	}
}
