#include "Math/Transform.h"

namespace Canape
{
	namespace
	{
		float SafeReciprocal(float value)
		{
			return Math::Abs(value) < Math::Epsilon ? 0.0f : 1.0f / value;
		}

		Vector3 SafeReciprocal(const Vector3& v)
		{
			return { SafeReciprocal(v.X), SafeReciprocal(v.Y), SafeReciprocal(v.Z) };
		}
	}

	Transform Transform::FromMatrix(const Matrix4& matrix)
	{
		Transform result;
		matrix.Decompose(result.Position, result.Rotation, result.Scale);
		return result;
	}

	Transform Transform::Inverse() const
	{
		const Quaternion inverseRotation = Rotation.Inverse();
		const Vector3 inverseScale = SafeReciprocal(Scale);
		const Vector3 inversePosition = inverseRotation.Rotate(-Position) * inverseScale;
		return { inversePosition, inverseRotation, inverseScale };
	}

	Vector3 Transform::InverseTransformPoint(const Vector3& point) const
	{
		return Rotation.Unrotate(point - Position) * SafeReciprocal(Scale);
	}

	Vector3 Transform::InverseTransformVector(const Vector3& vector) const
	{
		return Rotation.Unrotate(vector) * SafeReciprocal(Scale);
	}

	void Transform::LookAt(const Vector3& target, const Vector3& up)
	{
		const Vector3 direction = target - Position;
		if (!direction.IsNearlyZero())
		{
			Rotation = Quaternion::LookRotation(direction, up);
		}
	}

	Transform Transform::operator*(const Transform& parent) const
	{
		return {
			parent.TransformPoint(Position),
			(parent.Rotation * Rotation).Normalized(),
			parent.Scale * Scale
		};
	}

	Transform Transform::Lerp(const Transform& a, const Transform& b, float t)
	{
		return {
			Vector3::Lerp(a.Position, b.Position, t),
			Quaternion::Slerp(a.Rotation, b.Rotation, t),
			Vector3::Lerp(a.Scale, b.Scale, t)
		};
	}

	bool Transform::IsNearlyEqual(const Transform& a, const Transform& b, float tolerance)
	{
		return Vector3::IsNearlyEqual(a.Position, b.Position, tolerance)
			&& Quaternion::IsNearlyEqual(a.Rotation, b.Rotation, tolerance)
			&& Vector3::IsNearlyEqual(a.Scale, b.Scale, tolerance);
	}
}
