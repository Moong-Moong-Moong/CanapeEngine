#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace Canape::Math
{
	inline constexpr float Pi = 3.14159265358979323846f;
	inline constexpr float TwoPi = Pi * 2.0f;
	inline constexpr float HalfPi = Pi * 0.5f;
	inline constexpr float InvPi = 1.0f / Pi;
	inline constexpr float DegToRad = Pi / 180.0f;
	inline constexpr float RadToDeg = 180.0f / Pi;

	inline constexpr float Epsilon = 1.0e-6f;
	inline constexpr float SmallNumber = 1.0e-4f;
	inline constexpr float Infinity = std::numeric_limits<float>::infinity();
	inline constexpr float MaxFloat = std::numeric_limits<float>::max();

	inline constexpr float Meter = 1.0f;
	inline constexpr float Centimeter = 0.01f;
	inline constexpr float Millimeter = 0.001f;
	inline constexpr float Kilometer = 1000.0f;
	inline constexpr float Gravity = 9.81f;

	template<typename T>
	constexpr T Min(T a, T b) { return a < b ? a : b; }

	template<typename T>
	constexpr T Max(T a, T b) { return a > b ? a : b; }

	template<typename T>
	constexpr T Clamp(T value, T min, T max) { return value < min ? min : (value > max ? max : value); }

	template<typename T>
	constexpr T Abs(T value) { return value < T(0) ? -value : value; }

	template<typename T>
	constexpr T Sign(T value) { return value > T(0) ? T(1) : (value < T(0) ? T(-1) : T(0)); }

	template<typename T>
	constexpr T Square(T value) { return value * value; }

	constexpr float Saturate(float value) { return Clamp(value, 0.0f, 1.0f); }

	constexpr float ToRadians(float degrees) { return degrees * DegToRad; }
	constexpr float ToDegrees(float radians) { return radians * RadToDeg; }

	constexpr float Lerp(float a, float b, float t) { return a + (b - a) * t; }

	constexpr float InverseLerp(float a, float b, float value)
	{
		return a == b ? 0.0f : (value - a) / (b - a);
	}

	constexpr float Remap(float value, float inMin, float inMax, float outMin, float outMax)
	{
		return Lerp(outMin, outMax, InverseLerp(inMin, inMax, value));
	}

	constexpr float SmoothStep(float edge0, float edge1, float x)
	{
		const float t = Saturate(InverseLerp(edge0, edge1, x));
		return t * t * (3.0f - 2.0f * t);
	}

	constexpr bool IsNearlyEqual(float a, float b, float tolerance = SmallNumber)
	{
		return Abs(a - b) <= tolerance;
	}

	constexpr bool IsNearlyZero(float value, float tolerance = SmallNumber)
	{
		return Abs(value) <= tolerance;
	}

	inline float Sqrt(float value) { return std::sqrt(value); }
	inline float InvSqrt(float value) { return 1.0f / std::sqrt(value); }
	inline float Sin(float radians) { return std::sin(radians); }
	inline float Cos(float radians) { return std::cos(radians); }
	inline float Tan(float radians) { return std::tan(radians); }
	inline float Asin(float value) { return std::asin(Clamp(value, -1.0f, 1.0f)); }
	inline float Acos(float value) { return std::acos(Clamp(value, -1.0f, 1.0f)); }
	inline float Atan(float value) { return std::atan(value); }
	inline float Atan2(float y, float x) { return std::atan2(y, x); }
	inline float Floor(float value) { return std::floor(value); }
	inline float Ceil(float value) { return std::ceil(value); }
	inline float Round(float value) { return std::round(value); }
	inline float Trunc(float value) { return std::trunc(value); }
	inline float Frac(float value) { return value - std::floor(value); }
	inline float Fmod(float x, float y) { return std::fmod(x, y); }
	inline float Pow(float base, float exponent) { return std::pow(base, exponent); }
	inline float Exp(float value) { return std::exp(value); }
	inline float Log(float value) { return std::log(value); }
	inline float Log2(float value) { return std::log2(value); }

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

	inline float WrapRadians(float radians)
	{
		return Wrap(radians, -Pi, Pi);
	}

	inline float WrapDegrees(float degrees)
	{
		return Wrap(degrees, -180.0f, 180.0f);
	}

	inline float DeltaDegrees(float current, float target)
	{
		return WrapDegrees(target - current);
	}

	inline float DeltaRadians(float current, float target)
	{
		return WrapRadians(target - current);
	}

	inline float MoveTowards(float current, float target, float maxDelta)
	{
		if (Abs(target - current) <= maxDelta)
		{
			return target;
		}
		return current + Sign(target - current) * maxDelta;
	}

	inline float MoveTowardsDegrees(float current, float target, float maxDelta)
	{
		const float delta = DeltaDegrees(current, target);
		if (Abs(delta) <= maxDelta)
		{
			return current + delta;
		}
		return current + Sign(delta) * maxDelta;
	}

	inline float InterpTo(float current, float target, float deltaTime, float speed)
	{
		if (speed <= 0.0f)
		{
			return target;
		}

		const float distance = target - current;
		if (Square(distance) < Epsilon)
		{
			return target;
		}
		return current + distance * Saturate(deltaTime * speed);
	}

	inline float ExpDecay(float current, float target, float decay, float deltaTime)
	{
		return target + (current - target) * std::exp(-decay * deltaTime);
	}

	inline float SmoothDamp(float current, float target, float& velocity, float smoothTime, float deltaTime, float maxSpeed = Infinity)
	{
		if (deltaTime <= 0.0f)
		{
			return current;
		}

		smoothTime = Max(0.0001f, smoothTime);
		const float omega = 2.0f / smoothTime;
		const float x = omega * deltaTime;
		const float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

		const float originalTarget = target;
		const float maxChange = maxSpeed * smoothTime;
		const float change = Clamp(current - target, -maxChange, maxChange);
		target = current - change;

		const float temp = (velocity + omega * change) * deltaTime;
		velocity = (velocity - omega * temp) * exp;
		float output = target + (change + temp) * exp;

		if ((originalTarget - current > 0.0f) == (output > originalTarget))
		{
			output = originalTarget;
			velocity = (output - originalTarget) / deltaTime;
		}
		return output;
	}

	constexpr bool IsPowerOfTwo(uint32_t value)
	{
		return value != 0 && (value & (value - 1)) == 0;
	}

	constexpr uint32_t NextPowerOfTwo(uint32_t value)
	{
		if (value == 0)
		{
			return 1;
		}

		--value;
		value |= value >> 1;
		value |= value >> 2;
		value |= value >> 4;
		value |= value >> 8;
		value |= value >> 16;
		return value + 1;
	}

	template<typename T>
	constexpr T AlignUp(T value, T alignment)
	{
		return (value + alignment - 1) / alignment * alignment;
	}
}
