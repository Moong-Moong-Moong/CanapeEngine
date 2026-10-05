#pragma once

#include "Math/Quaternion.h"
#include "Math/Vector3.h"

namespace Canape
{
	struct Rotator
	{
		float Pitch = 0.0f;
		float Yaw = 0.0f;
		float Roll = 0.0f;

		constexpr Rotator() = default;
		constexpr Rotator(float pitch, float yaw, float roll) : Pitch(pitch), Yaw(yaw), Roll(roll) {}

		static const Rotator Zero;

		constexpr Rotator operator-() const { return { -Pitch, -Yaw, -Roll }; }
		constexpr Rotator operator+(const Rotator& rhs) const { return { Pitch + rhs.Pitch, Yaw + rhs.Yaw, Roll + rhs.Roll }; }
		constexpr Rotator operator-(const Rotator& rhs) const { return { Pitch - rhs.Pitch, Yaw - rhs.Yaw, Roll - rhs.Roll }; }
		constexpr Rotator operator*(float scalar) const { return { Pitch * scalar, Yaw * scalar, Roll * scalar }; }

		constexpr Rotator& operator+=(const Rotator& rhs) { Pitch += rhs.Pitch; Yaw += rhs.Yaw; Roll += rhs.Roll; return *this; }
		constexpr Rotator& operator-=(const Rotator& rhs) { Pitch -= rhs.Pitch; Yaw -= rhs.Yaw; Roll -= rhs.Roll; return *this; }

		constexpr bool operator==(const Rotator& rhs) const = default;

		Quaternion ToQuaternion() const;
		static Rotator FromQuaternion(const Quaternion& q);

		Rotator Normalized() const;
		void Normalize() { *this = Normalized(); }

		Vector3 GetRight() const { return ToQuaternion().GetRight(); }
		Vector3 GetUp() const { return ToQuaternion().GetUp(); }
		Vector3 GetForward() const { return ToQuaternion().GetForward(); }

		static Rotator Lerp(const Rotator& a, const Rotator& b, float t);
		static Rotator InterpTo(const Rotator& current, const Rotator& target, float deltaTime, float speed);
		static bool IsNearlyEqual(const Rotator& a, const Rotator& b, float toleranceDegrees = 0.01f);
	};

	inline constexpr Rotator Rotator::Zero{ 0.0f, 0.0f, 0.0f };
}
