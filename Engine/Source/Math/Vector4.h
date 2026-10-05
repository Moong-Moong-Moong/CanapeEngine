#pragma once

#include "Math/MathUtility.h"
#include "Math/Vector3.h"

namespace Canape
{
	struct Vector4
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 0.0f;

		constexpr Vector4() = default;
		constexpr Vector4(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}
		constexpr explicit Vector4(float scalar) : X(scalar), Y(scalar), Z(scalar), W(scalar) {}
		constexpr Vector4(const Vector3& xyz, float w) : X(xyz.X), Y(xyz.Y), Z(xyz.Z), W(w) {}

		static const Vector4 Zero;
		static const Vector4 One;

		constexpr float& operator[](int index)
		{
			switch (index)
			{
			case 0: return X;
			case 1: return Y;
			case 2: return Z;
			default: return W;
			}
		}

		constexpr float operator[](int index) const
		{
			switch (index)
			{
			case 0: return X;
			case 1: return Y;
			case 2: return Z;
			default: return W;
			}
		}

		constexpr Vector4 operator-() const { return { -X, -Y, -Z, -W }; }
		constexpr Vector4 operator+(const Vector4& rhs) const { return { X + rhs.X, Y + rhs.Y, Z + rhs.Z, W + rhs.W }; }
		constexpr Vector4 operator-(const Vector4& rhs) const { return { X - rhs.X, Y - rhs.Y, Z - rhs.Z, W - rhs.W }; }
		constexpr Vector4 operator*(const Vector4& rhs) const { return { X * rhs.X, Y * rhs.Y, Z * rhs.Z, W * rhs.W }; }
		constexpr Vector4 operator/(const Vector4& rhs) const { return { X / rhs.X, Y / rhs.Y, Z / rhs.Z, W / rhs.W }; }
		constexpr Vector4 operator*(float scalar) const { return { X * scalar, Y * scalar, Z * scalar, W * scalar }; }
		constexpr Vector4 operator/(float scalar) const { return { X / scalar, Y / scalar, Z / scalar, W / scalar }; }

		constexpr Vector4& operator+=(const Vector4& rhs) { X += rhs.X; Y += rhs.Y; Z += rhs.Z; W += rhs.W; return *this; }
		constexpr Vector4& operator-=(const Vector4& rhs) { X -= rhs.X; Y -= rhs.Y; Z -= rhs.Z; W -= rhs.W; return *this; }
		constexpr Vector4& operator*=(const Vector4& rhs) { X *= rhs.X; Y *= rhs.Y; Z *= rhs.Z; W *= rhs.W; return *this; }
		constexpr Vector4& operator/=(const Vector4& rhs) { X /= rhs.X; Y /= rhs.Y; Z /= rhs.Z; W /= rhs.W; return *this; }
		constexpr Vector4& operator*=(float scalar) { X *= scalar; Y *= scalar; Z *= scalar; W *= scalar; return *this; }
		constexpr Vector4& operator/=(float scalar) { X /= scalar; Y /= scalar; Z /= scalar; W /= scalar; return *this; }

		constexpr bool operator==(const Vector4& rhs) const = default;

		constexpr float LengthSquared() const { return X * X + Y * Y + Z * Z + W * W; }
		float Length() const { return Math::Sqrt(LengthSquared()); }

		Vector4 Normalized() const
		{
			const float length = Length();
			return length > Math::Epsilon ? *this / length : Vector4{};
		}

		void Normalize() { *this = Normalized(); }

		constexpr Vector3 XYZ() const { return { X, Y, Z }; }

		constexpr bool IsNearlyZero(float tolerance = Math::SmallNumber) const
		{
			return Math::Abs(X) <= tolerance && Math::Abs(Y) <= tolerance && Math::Abs(Z) <= tolerance && Math::Abs(W) <= tolerance;
		}

		static constexpr float Dot(const Vector4& a, const Vector4& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W; }
		static constexpr Vector4 Lerp(const Vector4& a, const Vector4& b, float t) { return a + (b - a) * t; }

		static constexpr Vector4 Min(const Vector4& a, const Vector4& b)
		{
			return { Math::Min(a.X, b.X), Math::Min(a.Y, b.Y), Math::Min(a.Z, b.Z), Math::Min(a.W, b.W) };
		}

		static constexpr Vector4 Max(const Vector4& a, const Vector4& b)
		{
			return { Math::Max(a.X, b.X), Math::Max(a.Y, b.Y), Math::Max(a.Z, b.Z), Math::Max(a.W, b.W) };
		}

		static constexpr bool IsNearlyEqual(const Vector4& a, const Vector4& b, float tolerance = Math::SmallNumber)
		{
			return (a - b).IsNearlyZero(tolerance);
		}
	};

	inline constexpr Vector4 Vector4::Zero{ 0.0f, 0.0f, 0.0f, 0.0f };
	inline constexpr Vector4 Vector4::One{ 1.0f, 1.0f, 1.0f, 1.0f };

	constexpr Vector4 operator*(float scalar, const Vector4& v) { return v * scalar; }
}
