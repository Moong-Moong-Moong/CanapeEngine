#pragma once

#include <cmath>

#include "Math/Vector3.h"
#include "JoltPhysics/Math/ScalarExt.h"

// TODO. move to Vector3.h later.
namespace Canape
{
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
}