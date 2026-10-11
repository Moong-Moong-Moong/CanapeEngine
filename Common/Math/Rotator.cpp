#include "Math/Rotator.h"

namespace Canape
{
	Quaternion Rotator::ToQuaternion() const
	{
		const Quaternion yaw = Quaternion::FromAxisAngle(Vector3::Up, Math::ToRadians(Yaw));
		const Quaternion pitch = Quaternion::FromAxisAngle(Vector3::Right, Math::ToRadians(Pitch));
		const Quaternion roll = Quaternion::FromAxisAngle(Vector3::Forward, Math::ToRadians(Roll));
		return yaw * pitch * roll;
	}

	Rotator Rotator::FromQuaternion(const Quaternion& rotation)
	{
		const Quaternion q = rotation.Normalized();

		const float m00 = 1.0f - 2.0f * (q.Y * q.Y + q.Z * q.Z);
		const float m01 = 2.0f * (q.X * q.Y + q.Z * q.W);
		const float m02 = 2.0f * (q.X * q.Z - q.Y * q.W);
		const float m11 = 1.0f - 2.0f * (q.X * q.X + q.Z * q.Z);
		const float m20 = 2.0f * (q.X * q.Z + q.Y * q.W);
		const float m21 = 2.0f * (q.Y * q.Z - q.X * q.W);
		const float m22 = 1.0f - 2.0f * (q.X * q.X + q.Y * q.Y);

		const float sinPitch = -m21;
		Rotator result;
		if (Math::Abs(sinPitch) >= 0.9999f)
		{
			result.Pitch = sinPitch > 0.0f ? 90.0f : -90.0f;
			result.Yaw = Math::ToDegrees(Math::Atan2(-m02, m00));
			result.Roll = 0.0f;
		}
		else
		{
			result.Pitch = Math::ToDegrees(Math::Asin(sinPitch));
			result.Yaw = Math::ToDegrees(Math::Atan2(m20, m22));
			result.Roll = Math::ToDegrees(Math::Atan2(m01, m11));
		}
		return result;
	}

	Rotator Rotator::Normalized() const
	{
		return { Math::WrapDegrees(Pitch), Math::WrapDegrees(Yaw), Math::WrapDegrees(Roll) };
	}

	Rotator Rotator::Lerp(const Rotator& a, const Rotator& b, float t)
	{
		return {
			a.Pitch + Math::DeltaDegrees(a.Pitch, b.Pitch) * t,
			a.Yaw + Math::DeltaDegrees(a.Yaw, b.Yaw) * t,
			a.Roll + Math::DeltaDegrees(a.Roll, b.Roll) * t
		};
	}

	Rotator Rotator::InterpTo(const Rotator& current, const Rotator& target, float deltaTime, float speed)
	{
		if (speed <= 0.0f)
		{
			return target;
		}
		return Lerp(current, target, Math::Saturate(deltaTime * speed));
	}

	bool Rotator::IsNearlyEqual(const Rotator& a, const Rotator& b, float toleranceDegrees)
	{
		return Math::Abs(Math::DeltaDegrees(a.Pitch, b.Pitch)) <= toleranceDegrees
			&& Math::Abs(Math::DeltaDegrees(a.Yaw, b.Yaw)) <= toleranceDegrees
			&& Math::Abs(Math::DeltaDegrees(a.Roll, b.Roll)) <= toleranceDegrees;
	}
}
