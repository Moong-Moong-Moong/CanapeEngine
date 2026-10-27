#pragma once

#include "Math/MathUtility.h"
#include "Math/Quaternion.h"

#include <DirectXMath.h>

namespace Canape
{
	struct Rotator
	{
		float Pitch = 0.0f;
		float Yaw = 0.0f;
		float Roll = 0.0f;

		constexpr Rotator() = default;
		constexpr Rotator(float pitch, float yaw, float roll) : Pitch(pitch), Yaw(yaw), Roll(roll) {}

		Quaternion ToQuaternion() const
		{
			return Quaternion::FromXM(DirectX::XMQuaternionRotationRollPitchYaw(
				Math::ToRadians(Pitch), Math::ToRadians(Yaw), Math::ToRadians(Roll)));
		}
	};
}
