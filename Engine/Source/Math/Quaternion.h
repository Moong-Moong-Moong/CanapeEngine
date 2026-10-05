#pragma once

#include "Math/Vector3.h"

namespace Canape
{
	struct Matrix4;

	struct Quaternion
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 1.0f;

		constexpr Quaternion() = default;
		constexpr Quaternion(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}

		static const Quaternion Identity;

		static Quaternion FromAxisAngle(const Vector3& axis, float radians);
		static Quaternion FromRotationMatrix(const Matrix4& matrix);
		static Quaternion LookRotation(const Vector3& forward, const Vector3& up = Vector3::Up);
		static Quaternion FromToRotation(const Vector3& from, const Vector3& to);

		static constexpr float Dot(const Quaternion& a, const Quaternion& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W; }
		static float Angle(const Quaternion& a, const Quaternion& b);
		static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t);
		static Quaternion Nlerp(const Quaternion& a, const Quaternion& b, float t);
		static Quaternion RotateTowards(const Quaternion& from, const Quaternion& to, float maxRadiansDelta);
		static bool IsNearlyEqual(const Quaternion& a, const Quaternion& b, float tolerance = Math::SmallNumber);

		constexpr Quaternion operator*(const Quaternion& rhs) const
		{
			return {
				W * rhs.X + X * rhs.W + Y * rhs.Z - Z * rhs.Y,
				W * rhs.Y - X * rhs.Z + Y * rhs.W + Z * rhs.X,
				W * rhs.Z + X * rhs.Y - Y * rhs.X + Z * rhs.W,
				W * rhs.W - X * rhs.X - Y * rhs.Y - Z * rhs.Z
			};
		}

		constexpr Quaternion& operator*=(const Quaternion& rhs) { *this = *this * rhs; return *this; }
		constexpr Vector3 operator*(const Vector3& v) const { return Rotate(v); }

		constexpr bool operator==(const Quaternion& rhs) const = default;

		constexpr float LengthSquared() const { return X * X + Y * Y + Z * Z + W * W; }
		float Length() const { return Math::Sqrt(LengthSquared()); }

		Quaternion Normalized() const;
		void Normalize() { *this = Normalized(); }

		constexpr Quaternion Conjugate() const { return { -X, -Y, -Z, W }; }

		constexpr Quaternion Inverse() const
		{
			const float lengthSquared = LengthSquared();
			if (lengthSquared < Math::Epsilon)
			{
				return Quaternion{};
			}
			const float inv = 1.0f / lengthSquared;
			return { -X * inv, -Y * inv, -Z * inv, W * inv };
		}

		constexpr Vector3 Rotate(const Vector3& v) const
		{
			const Vector3 u{ X, Y, Z };
			const Vector3 t = Vector3::Cross(u, v) * 2.0f;
			return v + t * W + Vector3::Cross(u, t);
		}

		constexpr Vector3 Unrotate(const Vector3& v) const { return Conjugate().Rotate(v); }

		constexpr Vector3 GetRight() const { return Rotate(Vector3::Right); }
		constexpr Vector3 GetUp() const { return Rotate(Vector3::Up); }
		constexpr Vector3 GetForward() const { return Rotate(Vector3::Forward); }

		void ToAxisAngle(Vector3& outAxis, float& outRadians) const;
		Matrix4 ToMatrix() const;
	};

	inline constexpr Quaternion Quaternion::Identity{ 0.0f, 0.0f, 0.0f, 1.0f };
}
