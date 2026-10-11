#pragma once

#include "Math/Matrix4.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"

namespace Canape
{
	struct Transform
	{
		Vector3 Position = Vector3::Zero;
		Quaternion Rotation = Quaternion::Identity;
		Vector3 Scale = Vector3::One;

		constexpr Transform() = default;
		constexpr Transform(const Vector3& position, const Quaternion& rotation = Quaternion::Identity, const Vector3& scale = Vector3::One)
			: Position(position), Rotation(rotation), Scale(scale)
		{
		}

		static const Transform Identity;

		Matrix4 ToMatrix() const { return Matrix4::TRS(Position, Rotation, Scale); }
		static Transform FromMatrix(const Matrix4& matrix);

		Transform Inverse() const;

		constexpr Vector3 TransformPoint(const Vector3& point) const { return Rotation.Rotate(point * Scale) + Position; }
		constexpr Vector3 TransformVector(const Vector3& vector) const { return Rotation.Rotate(vector * Scale); }
		constexpr Vector3 TransformDirection(const Vector3& direction) const { return Rotation.Rotate(direction); }

		Vector3 InverseTransformPoint(const Vector3& point) const;
		Vector3 InverseTransformVector(const Vector3& vector) const;
		constexpr Vector3 InverseTransformDirection(const Vector3& direction) const { return Rotation.Unrotate(direction); }

		constexpr Vector3 GetRight() const { return Rotation.GetRight(); }
		constexpr Vector3 GetUp() const { return Rotation.GetUp(); }
		constexpr Vector3 GetForward() const { return Rotation.GetForward(); }

		void Translate(const Vector3& delta) { Position += delta; }
		void Rotate(const Quaternion& delta) { Rotation = (delta * Rotation).Normalized(); }
		void LookAt(const Vector3& target, const Vector3& up = Vector3::Up);

		Transform operator*(const Transform& parent) const;

		static Transform Lerp(const Transform& a, const Transform& b, float t);
		static bool IsNearlyEqual(const Transform& a, const Transform& b, float tolerance = Math::SmallNumber);
	};

	inline constexpr Transform Transform::Identity{};
}
