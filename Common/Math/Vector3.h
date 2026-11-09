#pragma once

#include "Math/MathUtility.h"
#include "Math/Scalar.h"

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

	inline constexpr Vector3 operator*(const Vector3& lhs, const Vector3& rhs)
    {
        return Vector3(lhs.X * rhs.X, lhs.Y * rhs.Y, lhs.Z * rhs.Z);
    }
	inline constexpr Vector3 operator/(const Vector3& lhs, const Vector3& rhs)
    {
        return Vector3(lhs.X / rhs.X, lhs.Y / rhs.Y, lhs.Z / rhs.Z);
    }
    inline constexpr Vector3& operator*=(Vector3& lhs, const Vector3& rhs)
    {
        lhs.X *= rhs.X; lhs.Y *= rhs.Y; lhs.Z *= rhs.Z;
        return lhs;
    }
	inline constexpr Vector3& operator/=(Vector3& lhs, const Vector3& rhs)
    {
        lhs.X /= rhs.X; lhs.Y /= rhs.Y; lhs.Z /= rhs.Z;
        return lhs;
    }

    inline constexpr Vector3 Abs(const Vector3& v)
    {
        return { std::fabs(v.X), std::fabs(v.Y), std::fabs(v.Z) };
    }

    inline constexpr bool operator==(const Vector3& lhs, const Vector3& rhs)
    {
        return lhs.X == rhs.X && lhs.Y == rhs.Y && lhs.Z == rhs.Z;
    }
    inline constexpr bool operator!=(const Vector3& lhs, const Vector3& rhs)
    {
        return !(lhs == rhs);
    }

    // Notice! v1 < v2 || v1 >= v2 could be false
    inline constexpr bool operator<(const Vector3& lhs, const Vector3& rhs)
    {
        return lhs.X < rhs.X && lhs.Y < rhs.Y && lhs.Z < rhs.Z;
    }
    inline constexpr bool operator<=(const Vector3& lhs, const Vector3& rhs)
    {
        return lhs.X <= rhs.X && lhs.Y <= rhs.Y && lhs.Z <= rhs.Z;
    }
    inline constexpr bool operator>(const Vector3& lhs, const Vector3& rhs)
    {
        return lhs.X > rhs.X && lhs.Y > rhs.Y && lhs.Z > rhs.Z;
    }
    inline constexpr bool operator>=(const Vector3& lhs, const Vector3& rhs)
    {
        return lhs.X >= rhs.X && lhs.Y >= rhs.Y && lhs.Z >= rhs.Z;
    }

    inline bool IsFinite(const Vector3& v)
    {
        return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
    }

    inline bool IsPositive(const Vector3& v)
    {
        return IsFinite(v) && Vector3::Zero < v;
    }
    inline bool IsNonNegative(const Vector3& v)
    {
        return IsFinite(v) && Vector3::Zero <= v;
    }

	inline bool IsUniform(const Vector3& scale, float UniformScaleTolerance = 1.0e-4f)
	{
        const auto v = Abs(scale);
		const auto largest = Math::Max(v.X, Math::Max(v.Y, v.Z));
		const auto smallest = Math::Min(v.X, Math::Min(v.Y, v.Z));
		return largest - smallest <= UniformScaleTolerance * largest;
	}

    inline constexpr Vector3 ToVector3(float f)
    {
        return Vector3(f, f, f);
    }

	inline bool IsUsableExtent(const Vector3& extent, float MinExtent = 1.0e-4f)
	{
        return IsFinite(extent) && extent >= ToVector3(MinExtent);
	}

    inline bool IsUnit(const Vector3& v, float UnitLengthSquaredTolerance = 1.0e-3f)
    {
        return IsFinite(v) && std::fabs(v.LengthSquared() - 1.0f) <= UnitLengthSquaredTolerance;
    }

    // A unit vector perpendicular to a unit vector: the cross product with the world axis it is least aligned with.
    inline Vector3 PerpendicularTo(const Vector3& unit)
    {
        const Vector3 magnitudes = Abs(unit);
        const Vector3 leastAligned = magnitudes.X <= magnitudes.Y && magnitudes.X <= magnitudes.Z ?
            Vector3::Right : magnitudes.Y <= magnitudes.Z ?
                Vector3::Up :
                Vector3::Forward;
        return Vector3::Cross(unit, leastAligned).Normalized();
    }

	inline constexpr bool SameBits(const Vector3& lhs, const Vector3& rhs)
	{
		return SameBits(lhs.X, rhs.X) && SameBits(lhs.Y, rhs.Y) && SameBits(lhs.Z, rhs.Z);
	}

    inline constexpr float Min(const Vector3& v)
    {
        return Math::Min(v.X, Math::Min(v.Y, v.Z));
    }
    inline constexpr float Max(const Vector3& v)
    {
        return Math::Max(v.X, Math::Max(v.Y, v.Z));
    }

    inline constexpr Vector3 Lerp(const Vector3& a, const Vector3& b, float s)
    {
        return Vector3{
            Lerp(a.X, b.X, s),
            Lerp(a.Y, b.Y, s),
            Lerp(a.Z, b.Z, s)
        };
    }

	inline constexpr Vector3 Vector3::Zero{ 0.0f, 0.0f, 0.0f };
	inline constexpr Vector3 Vector3::One{ 1.0f, 1.0f, 1.0f };
	inline constexpr Vector3 Vector3::Right{ 1.0f, 0.0f, 0.0f };
	inline constexpr Vector3 Vector3::Up{ 0.0f, 1.0f, 0.0f };
	inline constexpr Vector3 Vector3::Forward{ 0.0f, 0.0f, 1.0f };
}
