#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vec3.h>

#include "Math/Quaternion.h"
#include "Math/Vector3.h"

namespace Canape
{
    inline JPH::Vec3 ToJolt(const Vector3& v)
    {
        return JPH::Vec3(v.X, v.Y, v.Z);
    }

    inline JPH::Quat ToJolt(const Quaternion& q)
    {
        return JPH::Quat(q.X, q.Y, q.Z, q.W);
    }

    inline Vector3 ToCanape(JPH::Vec3Arg v)
    {
        return Vector3(v.GetX(), v.GetY(), v.GetZ());
    }

    inline Quaternion ToCanape(JPH::QuatArg q)
    {
        return Quaternion(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
    }
}
