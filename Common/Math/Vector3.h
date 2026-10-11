#pragma once

#include "Math/MathUtility.h"
#include "Math/Vector2.h"

namespace Canape
{
	struct Vector3
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;

		constexpr Vector3() = default;
		constexpr Vector3(float x, float y, float z) : X(x), Y(y), Z(z) {}
		constexpr explicit Vector3(float scalar) : X(scalar), Y(scalar), Z(scalar) {}
		constexpr Vector3(const Vector2& xy, float z) : X(xy.X), Y(xy.Y), Z(z) {}

		static const Vector3 Zero;
		static const Vector3 One;
		static const Vector3 Right;
		static const Vector3 Left;
		static const Vector3 Up;
		static const Vector3 Down;
		static const Vector3 Forward;
		static const Vector3 Back;

		constexpr float& operator[](int index) { return index == 0 ? X : (index == 1 ? Y : Z); }
		constexpr float operator[](int index) const { return index == 0 ? X : (index == 1 ? Y : Z); }

		constexpr Vector3 operator-() const { return { -X, -Y, -Z }; }
		constexpr Vector3 operator+(const Vector3& rhs) const { return { X + rhs.X, Y + rhs.Y, Z + rhs.Z }; }
		constexpr Vector3 operator-(const Vector3& rhs) const { return { X - rhs.X, Y - rhs.Y, Z - rhs.Z }; }
		constexpr Vector3 operator*(const Vector3& rhs) const { return { X * rhs.X, Y * rhs.Y, Z * rhs.Z }; }
		constexpr Vector3 operator/(const Vector3& rhs) const { return { X / rhs.X, Y / rhs.Y, Z / rhs.Z }; }
		constexpr Vector3 operator*(float scalar) const { return { X * scalar, Y * scalar, Z * scalar }; }
		constexpr Vector3 operator/(float scalar) const { return { X / scalar, Y / scalar, Z / scalar }; }

		constexpr Vector3& operator+=(const Vector3& rhs) { X += rhs.X; Y += rhs.Y; Z += rhs.Z; return *this; }
		constexpr Vector3& operator-=(const Vector3& rhs) { X -= rhs.X; Y -= rhs.Y; Z -= rhs.Z; return *this; }
		constexpr Vector3& operator*=(const Vector3& rhs) { X *= rhs.X; Y *= rhs.Y; Z *= rhs.Z; return *this; }
		constexpr Vector3& operator/=(const Vector3& rhs) { X /= rhs.X; Y /= rhs.Y; Z /= rhs.Z; return *this; }
		constexpr Vector3& operator*=(float scalar) { X *= scalar; Y *= scalar; Z *= scalar; return *this; }
		constexpr Vector3& operator/=(float scalar) { X /= scalar; Y /= scalar; Z /= scalar; return *this; }

		constexpr bool operator==(const Vector3& rhs) const = default;

		constexpr float LengthSquared() const { return X * X + Y * Y + Z * Z; }
		float Length() const { return Math::Sqrt(LengthSquared()); }

		Vector3 Normalized() const
		{
			const float length = Length();
			return length > Math::Epsilon ? *this / length : Vector3{};
		}

		void Normalize() { *this = Normalized(); }

		constexpr bool IsNearlyZero(float tolerance = Math::SmallNumber) const
		{
			return Math::Abs(X) <= tolerance && Math::Abs(Y) <= tolerance && Math::Abs(Z) <= tolerance;
		}

		constexpr bool IsNormalized() const { return Math::IsNearlyEqual(LengthSquared(), 1.0f, 0.01f); }

		constexpr Vector2 XY() const { return { X, Y }; }
		constexpr Vector2 XZ() const { return { X, Z }; }

		constexpr float MinComponent() const { return Math::Min(X, Math::Min(Y, Z)); }
		constexpr float MaxComponent() const { return Math::Max(X, Math::Max(Y, Z)); }

		static constexpr float Dot(const Vector3& a, const Vector3& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

		static constexpr Vector3 Cross(const Vector3& a, const Vector3& b)
		{
			return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X };
		}

		static float Distance(const Vector3& a, const Vector3& b) { return (b - a).Length(); }
		static constexpr float DistanceSquared(const Vector3& a, const Vector3& b) { return (b - a).LengthSquared(); }
		static constexpr Vector3 Lerp(const Vector3& a, const Vector3& b, float t) { return a + (b - a) * t; }

		static constexpr Vector3 Min(const Vector3& a, const Vector3& b)
		{
			return { Math::Min(a.X, b.X), Math::Min(a.Y, b.Y), Math::Min(a.Z, b.Z) };
		}

		static constexpr Vector3 Max(const Vector3& a, const Vector3& b)
		{
			return { Math::Max(a.X, b.X), Math::Max(a.Y, b.Y), Math::Max(a.Z, b.Z) };
		}

		static constexpr Vector3 Abs(const Vector3& v) { return { Math::Abs(v.X), Math::Abs(v.Y), Math::Abs(v.Z) }; }

		static constexpr Vector3 Reflect(const Vector3& v, const Vector3& normal) { return v - normal * (2.0f * Dot(v, normal)); }

		static constexpr Vector3 Project(const Vector3& v, const Vector3& onNormal)
		{
			const float lengthSquared = onNormal.LengthSquared();
			if (lengthSquared < Math::Epsilon)
			{
				return {};
			}
			return onNormal * (Dot(v, onNormal) / lengthSquared);
		}

		static constexpr Vector3 ProjectOnPlane(const Vector3& v, const Vector3& planeNormal) { return v - Project(v, planeNormal); }

		static float Angle(const Vector3& from, const Vector3& to)
		{
			const float denominator = Math::Sqrt(from.LengthSquared() * to.LengthSquared());
			if (denominator < Math::Epsilon)
			{
				return 0.0f;
			}
			return Math::Acos(Dot(from, to) / denominator);
		}

		static float SignedAngle(const Vector3& from, const Vector3& to, const Vector3& axis)
		{
			const float angle = Angle(from, to);
			return Dot(Cross(from, to), axis) < 0.0f ? -angle : angle;
		}

		static Vector3 ClampLength(const Vector3& v, float maxLength)
		{
			const float lengthSquared = v.LengthSquared();
			if (lengthSquared > maxLength * maxLength && lengthSquared > 0.0f)
			{
				return v * (maxLength / Math::Sqrt(lengthSquared));
			}
			return v;
		}

		static Vector3 MoveTowards(const Vector3& current, const Vector3& target, float maxDistanceDelta)
		{
			const Vector3 delta = target - current;
			const float distance = delta.Length();
			if (distance <= maxDistanceDelta || distance < Math::Epsilon)
			{
				return target;
			}
			return current + delta / distance * maxDistanceDelta;
		}

		static Vector3 InterpTo(const Vector3& current, const Vector3& target, float deltaTime, float speed)
		{
			if (speed <= 0.0f)
			{
				return target;
			}

			const Vector3 distance = target - current;
			if (distance.LengthSquared() < Math::Epsilon)
			{
				return target;
			}
			return current + distance * Math::Saturate(deltaTime * speed);
		}

		static Vector3 SmoothDamp(const Vector3& current, const Vector3& target, Vector3& velocity, float smoothTime, float deltaTime, float maxSpeed = Math::Infinity)
		{
			return {
				Math::SmoothDamp(current.X, target.X, velocity.X, smoothTime, deltaTime, maxSpeed),
				Math::SmoothDamp(current.Y, target.Y, velocity.Y, smoothTime, deltaTime, maxSpeed),
				Math::SmoothDamp(current.Z, target.Z, velocity.Z, smoothTime, deltaTime, maxSpeed)
			};
		}

		static void OrthoNormalize(Vector3& normal, Vector3& tangent)
		{
			normal.Normalize();
			tangent = ProjectOnPlane(tangent, normal).Normalized();
		}

		static constexpr bool IsNearlyEqual(const Vector3& a, const Vector3& b, float tolerance = Math::SmallNumber)
		{
			return (a - b).IsNearlyZero(tolerance);
		}
	};

	inline constexpr Vector3 Vector3::Zero{ 0.0f, 0.0f, 0.0f };
	inline constexpr Vector3 Vector3::One{ 1.0f, 1.0f, 1.0f };
	inline constexpr Vector3 Vector3::Right{ 1.0f, 0.0f, 0.0f };
	inline constexpr Vector3 Vector3::Left{ -1.0f, 0.0f, 0.0f };
	inline constexpr Vector3 Vector3::Up{ 0.0f, 1.0f, 0.0f };
	inline constexpr Vector3 Vector3::Down{ 0.0f, -1.0f, 0.0f };
	inline constexpr Vector3 Vector3::Forward{ 0.0f, 0.0f, 1.0f };
	inline constexpr Vector3 Vector3::Back{ 0.0f, 0.0f, -1.0f };

	constexpr Vector3 operator*(float scalar, const Vector3& v) { return v * scalar; }
}
