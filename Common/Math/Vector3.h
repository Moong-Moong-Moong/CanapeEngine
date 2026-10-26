#pragma once

#include "Math/MathUtility.h"

#include <DirectXMath.h>

namespace Canape
{
	struct Vector3
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;

		constexpr Vector3() = default;
		constexpr Vector3(float x, float y, float z) : X(x), Y(y), Z(z) {}

		static const Vector3 Zero;
		static const Vector3 One;
		static const Vector3 Right;
		static const Vector3 Up;
		static const Vector3 Forward;

		DirectX::XMVECTOR ToXM() const { return DirectX::XMVectorSet(X, Y, Z, 0.0f); }

		static Vector3 FromXM(DirectX::FXMVECTOR v)
		{
			DirectX::XMFLOAT3 result;
			DirectX::XMStoreFloat3(&result, v);
			return { result.x, result.y, result.z };
		}

		constexpr Vector3 operator-() const { return { -X, -Y, -Z }; }
		constexpr Vector3 operator+(const Vector3& rhs) const { return { X + rhs.X, Y + rhs.Y, Z + rhs.Z }; }
		constexpr Vector3 operator-(const Vector3& rhs) const { return { X - rhs.X, Y - rhs.Y, Z - rhs.Z }; }
		constexpr Vector3 operator*(float scalar) const { return { X * scalar, Y * scalar, Z * scalar }; }
		constexpr Vector3 operator/(float scalar) const { return { X / scalar, Y / scalar, Z / scalar }; }

		constexpr Vector3& operator+=(const Vector3& rhs) { X += rhs.X; Y += rhs.Y; Z += rhs.Z; return *this; }
		constexpr Vector3& operator-=(const Vector3& rhs) { X -= rhs.X; Y -= rhs.Y; Z -= rhs.Z; return *this; }
		constexpr Vector3& operator*=(float scalar) { X *= scalar; Y *= scalar; Z *= scalar; return *this; }

		constexpr bool IsNearlyZero(float tolerance = Math::SmallNumber) const
		{
			return Math::Abs(X) <= tolerance && Math::Abs(Y) <= tolerance && Math::Abs(Z) <= tolerance;
		}

		float Length() const { return DirectX::XMVectorGetX(DirectX::XMVector3Length(ToXM())); }
		float LengthSquared() const { return DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(ToXM())); }

		Vector3 Normalized() const
		{
			return Length() > Math::Epsilon ? FromXM(DirectX::XMVector3Normalize(ToXM())) : Vector3{};
		}

		static float Dot(const Vector3& a, const Vector3& b)
		{
			return DirectX::XMVectorGetX(DirectX::XMVector3Dot(a.ToXM(), b.ToXM()));
		}

		static Vector3 Cross(const Vector3& a, const Vector3& b)
		{
			return FromXM(DirectX::XMVector3Cross(a.ToXM(), b.ToXM()));
		}

		static Vector3 ProjectOnPlane(const Vector3& v, const Vector3& planeNormal)
		{
			const float lengthSquared = planeNormal.LengthSquared();
			if (lengthSquared < Math::Epsilon)
			{
				return v;
			}
			return v - planeNormal * (Dot(v, planeNormal) / lengthSquared);
		}

		static Vector3 ClampLength(const Vector3& v, float maxLength)
		{
			return FromXM(DirectX::XMVector3ClampLength(v.ToXM(), 0.0f, maxLength));
		}
	};

	inline constexpr Vector3 Vector3::Zero{ 0.0f, 0.0f, 0.0f };
	inline constexpr Vector3 Vector3::One{ 1.0f, 1.0f, 1.0f };
	inline constexpr Vector3 Vector3::Right{ 1.0f, 0.0f, 0.0f };
	inline constexpr Vector3 Vector3::Up{ 0.0f, 1.0f, 0.0f };
	inline constexpr Vector3 Vector3::Forward{ 0.0f, 0.0f, 1.0f };
}
