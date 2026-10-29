#pragma once

#include "Math/MathUtility.h"
#include "Math/Vector3.h"

#include <DirectXMath.h>

namespace Canape
{
	struct Quaternion
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 1.0f;

		constexpr Quaternion() = default;
		constexpr Quaternion(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}

		static const Quaternion Identity;

		DirectX::XMVECTOR ToXM() const { return DirectX::XMVectorSet(X, Y, Z, W); }

		static Quaternion FromXM(DirectX::FXMVECTOR q)
		{
			DirectX::XMFLOAT4 result;
			DirectX::XMStoreFloat4(&result, q);
			return { result.x, result.y, result.z, result.w };
		}

		Quaternion operator*(const Quaternion& rhs) const
		{
			return FromXM(DirectX::XMQuaternionMultiply(rhs.ToXM(), ToXM()));
		}

		Quaternion Normalized() const
		{
			if (DirectX::XMVectorGetX(DirectX::XMQuaternionLength(ToXM())) < Math::Epsilon)
			{
				return Identity;
			}
			return FromXM(DirectX::XMQuaternionNormalize(ToXM()));
		}

		Quaternion Inverse() const
		{
			if (DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(ToXM())) < Math::Epsilon)
			{
				return Identity;
			}
			return FromXM(DirectX::XMQuaternionInverse(ToXM()));
		}

		Vector3 Rotate(const Vector3& v) const { return Vector3::FromXM(DirectX::XMVector3Rotate(v.ToXM(), ToXM())); }

		Vector3 GetRight() const { return Rotate(Vector3::Right); }
		Vector3 GetUp() const { return Rotate(Vector3::Up); }
		Vector3 GetForward() const { return Rotate(Vector3::Forward); }

		static Quaternion LookRotation(const Vector3& forward, const Vector3& up = Vector3::Up)
		{
			const Vector3 f = forward.Normalized();
			if (f.IsNearlyZero())
			{
				return Identity;
			}

			Vector3 r = Vector3::Cross(up, f);
			if (r.IsNearlyZero())
			{
				r = Vector3::Cross(Math::Abs(f.Y) < 0.999f ? Vector3::Up : Vector3::Forward, f);
			}
			r = r.Normalized();
			const Vector3 u = Vector3::Cross(f, r);

			const DirectX::XMMATRIX basis(
				r.X, r.Y, r.Z, 0.0f,
				u.X, u.Y, u.Z, 0.0f,
				f.X, f.Y, f.Z, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f);
			return FromXM(DirectX::XMQuaternionNormalize(DirectX::XMQuaternionRotationMatrix(basis)));
		}

		static Quaternion RotateTowards(const Quaternion& from, const Quaternion& to, float maxRadiansDelta)
		{
			const float dot = DirectX::XMVectorGetX(DirectX::XMQuaternionDot(from.ToXM(), to.ToXM()));
			const float angle = 2.0f * std::acos(Math::Min(Math::Abs(dot), 1.0f));
			if (angle < Math::Epsilon)
			{
				return to;
			}
			return FromXM(DirectX::XMQuaternionSlerp(from.ToXM(), to.ToXM(), Math::Min(1.0f, maxRadiansDelta / angle)));
		}
	};

	inline constexpr Quaternion Quaternion::Identity{ 0.0f, 0.0f, 0.0f, 1.0f };
}
