#pragma once

#include "Math/Vector2.h"
#include "Math/Vector3.h"

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
		static const IntVector2 One;

		constexpr IntVector2 operator-() const { return { -X, -Y }; }
		constexpr IntVector2 operator+(const IntVector2& rhs) const { return { X + rhs.X, Y + rhs.Y }; }
		constexpr IntVector2 operator-(const IntVector2& rhs) const { return { X - rhs.X, Y - rhs.Y }; }
		constexpr IntVector2 operator*(const IntVector2& rhs) const { return { X * rhs.X, Y * rhs.Y }; }
		constexpr IntVector2 operator*(int32_t scalar) const { return { X * scalar, Y * scalar }; }
		constexpr IntVector2 operator/(int32_t scalar) const { return { X / scalar, Y / scalar }; }

		constexpr IntVector2& operator+=(const IntVector2& rhs) { X += rhs.X; Y += rhs.Y; return *this; }
		constexpr IntVector2& operator-=(const IntVector2& rhs) { X -= rhs.X; Y -= rhs.Y; return *this; }

		constexpr bool operator==(const IntVector2& rhs) const = default;

		constexpr Vector2 ToVector2() const { return { static_cast<float>(X), static_cast<float>(Y) }; }

		static IntVector2 FromVector2(const Vector2& v)
		{
			return { static_cast<int32_t>(Math::Floor(v.X)), static_cast<int32_t>(Math::Floor(v.Y)) };
		}
	};

	struct IntVector3
	{
		int32_t X = 0;
		int32_t Y = 0;
		int32_t Z = 0;

		constexpr IntVector3() = default;
		constexpr IntVector3(int32_t x, int32_t y, int32_t z) : X(x), Y(y), Z(z) {}

		static const IntVector3 Zero;
		static const IntVector3 One;

		constexpr IntVector3 operator-() const { return { -X, -Y, -Z }; }
		constexpr IntVector3 operator+(const IntVector3& rhs) const { return { X + rhs.X, Y + rhs.Y, Z + rhs.Z }; }
		constexpr IntVector3 operator-(const IntVector3& rhs) const { return { X - rhs.X, Y - rhs.Y, Z - rhs.Z }; }
		constexpr IntVector3 operator*(const IntVector3& rhs) const { return { X * rhs.X, Y * rhs.Y, Z * rhs.Z }; }
		constexpr IntVector3 operator*(int32_t scalar) const { return { X * scalar, Y * scalar, Z * scalar }; }
		constexpr IntVector3 operator/(int32_t scalar) const { return { X / scalar, Y / scalar, Z / scalar }; }

		constexpr IntVector3& operator+=(const IntVector3& rhs) { X += rhs.X; Y += rhs.Y; Z += rhs.Z; return *this; }
		constexpr IntVector3& operator-=(const IntVector3& rhs) { X -= rhs.X; Y -= rhs.Y; Z -= rhs.Z; return *this; }

		constexpr bool operator==(const IntVector3& rhs) const = default;

		constexpr Vector3 ToVector3() const { return { static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z) }; }

		static IntVector3 FromVector3(const Vector3& v)
		{
			return {
				static_cast<int32_t>(Math::Floor(v.X)),
				static_cast<int32_t>(Math::Floor(v.Y)),
				static_cast<int32_t>(Math::Floor(v.Z))
			};
		}
	};

	inline constexpr IntVector2 IntVector2::Zero{ 0, 0 };
	inline constexpr IntVector2 IntVector2::One{ 1, 1 };
	inline constexpr IntVector3 IntVector3::Zero{ 0, 0, 0 };
	inline constexpr IntVector3 IntVector3::One{ 1, 1, 1 };
}
