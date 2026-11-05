#pragma once

#include <cmath>

#include "Math/Quaternion.h"
#include "JoltPhysics/Math/ScalarExt.h"

// TODO. move to Quaternion.h later.
namespace Canape
{
    inline constexpr bool operator==(const Quaternion& lhs, const Quaternion& rhs)
    {
        return lhs.X == rhs.X && lhs.Y == rhs.Y && lhs.Z == rhs.Z && lhs.W == rhs.W;
    }
    inline constexpr bool operator!=(const Quaternion& lhs, const Quaternion& rhs)
    {
        return !(lhs == rhs);
    }

    inline bool IsFinite(const Quaternion& q)
    {
        return std::isfinite(q.X) && std::isfinite(q.Y) && std::isfinite(q.Z) && std::isfinite(q.W);
    }

    inline float LengthSquared(const Quaternion& q)
    {
        return DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(q.ToXM()));
    }

    inline bool IsUsableRotation(const Quaternion& q, float UnitLengthSquaredTolerance = 1.0e-3f)
    {
        if (!IsFinite(q)) return false;

        const float lengthSquared = LengthSquared(q);
        return std::abs(lengthSquared - 1.0f) <= UnitLengthSquaredTolerance;
    }

	inline constexpr bool SameBits(const Quaternion& lhs, const Quaternion& rhs)
	{
		return SameBits(lhs.X, rhs.X) && SameBits(lhs.Y, rhs.Y) && SameBits(lhs.Z, rhs.Z) && SameBits(lhs.W, rhs.W);
	}
}