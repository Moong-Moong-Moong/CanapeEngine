#pragma once

#include "Math/MathUtility.h"

namespace Canape
{
	struct Vector2
	{
		float X = 0.0f;
		float Y = 0.0f;

		constexpr Vector2() = default;
		constexpr Vector2(float x, float y) : X(x), Y(y) {}
		constexpr explicit Vector2(float scalar) : X(scalar), Y(scalar) {}

		static const Vector2 Zero;
		static const Vector2 One;
		static const Vector2 UnitX;
		static const Vector2 UnitY;

		constexpr float& operator[](int index) { return index == 0 ? X : Y; }
		constexpr float operator[](int index) const { return index == 0 ? X : Y; }

		constexpr Vector2 operator-() const { return { -X, -Y }; }
		constexpr Vector2 operator+(const Vector2& rhs) const { return { X + rhs.X, Y + rhs.Y }; }
		constexpr Vector2 operator-(const Vector2& rhs) const { return { X - rhs.X, Y - rhs.Y }; }
		constexpr Vector2 operator*(const Vector2& rhs) const { return { X * rhs.X, Y * rhs.Y }; }
		constexpr Vector2 operator/(const Vector2& rhs) const { return { X / rhs.X, Y / rhs.Y }; }
		constexpr Vector2 operator*(float scalar) const { return { X * scalar, Y * scalar }; }
		constexpr Vector2 operator/(float scalar) const { return { X / scalar, Y / scalar }; }

		constexpr Vector2& operator+=(const Vector2& rhs) { X += rhs.X; Y += rhs.Y; return *this; }
		constexpr Vector2& operator-=(const Vector2& rhs) { X -= rhs.X; Y -= rhs.Y; return *this; }
		constexpr Vector2& operator*=(const Vector2& rhs) { X *= rhs.X; Y *= rhs.Y; return *this; }
		constexpr Vector2& operator/=(const Vector2& rhs) { X /= rhs.X; Y /= rhs.Y; return *this; }
		constexpr Vector2& operator*=(float scalar) { X *= scalar; Y *= scalar; return *this; }
		constexpr Vector2& operator/=(float scalar) { X /= scalar; Y /= scalar; return *this; }

		constexpr bool operator==(const Vector2& rhs) const = default;

		constexpr float LengthSquared() const { return X * X + Y * Y; }
		float Length() const { return Math::Sqrt(LengthSquared()); }

		Vector2 Normalized() const
		{
			const float length = Length();
			return length > Math::Epsilon ? *this / length : Vector2{};
		}

		void Normalize() { *this = Normalized(); }

		constexpr bool IsNearlyZero(float tolerance = Math::SmallNumber) const
		{
			return Math::Abs(X) <= tolerance && Math::Abs(Y) <= tolerance;
		}

		constexpr bool IsNormalized() const { return Math::IsNearlyEqual(LengthSquared(), 1.0f, 0.01f); }

		constexpr float MinComponent() const { return Math::Min(X, Y); }
		constexpr float MaxComponent() const { return Math::Max(X, Y); }

		static constexpr float Dot(const Vector2& a, const Vector2& b) { return a.X * b.X + a.Y * b.Y; }
		static constexpr float Cross(const Vector2& a, const Vector2& b) { return a.X * b.Y - a.Y * b.X; }
		static float Distance(const Vector2& a, const Vector2& b) { return (b - a).Length(); }
		static constexpr float DistanceSquared(const Vector2& a, const Vector2& b) { return (b - a).LengthSquared(); }
		static constexpr Vector2 Lerp(const Vector2& a, const Vector2& b, float t) { return a + (b - a) * t; }
		static constexpr Vector2 Min(const Vector2& a, const Vector2& b) { return { Math::Min(a.X, b.X), Math::Min(a.Y, b.Y) }; }
		static constexpr Vector2 Max(const Vector2& a, const Vector2& b) { return { Math::Max(a.X, b.X), Math::Max(a.Y, b.Y) }; }
		static constexpr Vector2 Abs(const Vector2& v) { return { Math::Abs(v.X), Math::Abs(v.Y) }; }
		static constexpr Vector2 Perpendicular(const Vector2& v) { return { -v.Y, v.X }; }
		static constexpr Vector2 Reflect(const Vector2& v, const Vector2& normal) { return v - normal * (2.0f * Dot(v, normal)); }

		static float Angle(const Vector2& from, const Vector2& to)
		{
			const float denominator = Math::Sqrt(from.LengthSquared() * to.LengthSquared());
			if (denominator < Math::Epsilon)
			{
				return 0.0f;
			}
			return Math::Acos(Dot(from, to) / denominator);
		}

		static float SignedAngle(const Vector2& from, const Vector2& to)
		{
			return Math::Atan2(Cross(from, to), Dot(from, to));
		}

		static Vector2 ClampLength(const Vector2& v, float maxLength)
		{
			const float lengthSquared = v.LengthSquared();
			if (lengthSquared > maxLength * maxLength && lengthSquared > 0.0f)
			{
				return v * (maxLength / Math::Sqrt(lengthSquared));
			}
			return v;
		}

		static Vector2 MoveTowards(const Vector2& current, const Vector2& target, float maxDistanceDelta)
		{
			const Vector2 delta = target - current;
			const float distance = delta.Length();
			if (distance <= maxDistanceDelta || distance < Math::Epsilon)
			{
				return target;
			}
			return current + delta / distance * maxDistanceDelta;
		}

		static constexpr bool IsNearlyEqual(const Vector2& a, const Vector2& b, float tolerance = Math::SmallNumber)
		{
			return (a - b).IsNearlyZero(tolerance);
		}
	};

	inline constexpr Vector2 Vector2::Zero{ 0.0f, 0.0f };
	inline constexpr Vector2 Vector2::One{ 1.0f, 1.0f };
	inline constexpr Vector2 Vector2::UnitX{ 1.0f, 0.0f };
	inline constexpr Vector2 Vector2::UnitY{ 0.0f, 1.0f };

	constexpr Vector2 operator*(float scalar, const Vector2& v) { return v * scalar; }
}
