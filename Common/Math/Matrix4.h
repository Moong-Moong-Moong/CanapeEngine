#pragma once

#include "Math/MathUtility.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"

#include <DirectXMath.h>

namespace Canape
{
	struct Matrix4
	{
		float M[4][4];

		constexpr Matrix4()
			: M{ { 1.0f, 0.0f, 0.0f, 0.0f },
				 { 0.0f, 1.0f, 0.0f, 0.0f },
				 { 0.0f, 0.0f, 1.0f, 0.0f },
				 { 0.0f, 0.0f, 0.0f, 1.0f } }
		{
		}

		static const Matrix4 Identity;

		DirectX::XMMATRIX ToXM() const { return DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(M)); }

		static Matrix4 FromXM(DirectX::FXMMATRIX m)
		{
			Matrix4 result;
			DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(result.M), m);
			return result;
		}

		float* Data() { return &M[0][0]; }
		const float* Data() const { return &M[0][0]; }

		constexpr Vector3 GetTranslation() const { return { M[3][0], M[3][1], M[3][2] }; }

		Matrix4 operator*(const Matrix4& rhs) const { return FromXM(DirectX::XMMatrixMultiply(ToXM(), rhs.ToXM())); }

		Vector3 TransformPoint(const Vector3& p) const
		{
			return Vector3::FromXM(DirectX::XMVector3Transform(p.ToXM(), ToXM()));
		}

		Matrix4 Inverse() const
		{
			DirectX::XMVECTOR determinant;
			const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(&determinant, ToXM());
			if (Math::Abs(DirectX::XMVectorGetX(determinant)) < Math::Epsilon * Math::Epsilon)
			{
				return Identity;
			}
			return FromXM(inverse);
		}

		static Matrix4 TRS(const Vector3& translation, const Quaternion& rotation, const Vector3& scale)
		{
			return FromXM(
				DirectX::XMMatrixScaling(scale.X, scale.Y, scale.Z) *
				DirectX::XMMatrixRotationQuaternion(rotation.Normalized().ToXM()) *
				DirectX::XMMatrixTranslation(translation.X, translation.Y, translation.Z));
		}

		static Matrix4 LookTo(const Vector3& eye, const Vector3& direction, const Vector3& up = Vector3::Up)
		{
			return FromXM(DirectX::XMMatrixLookToLH(eye.ToXM(), direction.ToXM(), up.ToXM()));
		}

		static Matrix4 Perspective(float fovYRadians, float aspectRatio, float nearZ, float farZ)
		{
			return FromXM(DirectX::XMMatrixPerspectiveFovLH(fovYRadians, aspectRatio, nearZ, farZ));
		}
	};

	static_assert(sizeof(Matrix4) == sizeof(DirectX::XMFLOAT4X4));

	inline constexpr Matrix4 Matrix4::Identity{};
}
