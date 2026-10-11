#include "Math/Matrix4.h"

namespace Canape
{
	namespace
	{
		void ComputeCofactors(const float* m, float* inv)
		{
			inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
			inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
			inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
			inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
			inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
			inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
			inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
			inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
			inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
			inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
			inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
			inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
			inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
			inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
			inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
			inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
		}
	}

	Vector3 Matrix4::ProjectPoint(const Vector3& p) const
	{
		const Vector4 result = Transform(Vector4{ p, 1.0f });
		if (Math::Abs(result.W) < Math::Epsilon)
		{
			return result.XYZ();
		}
		return result.XYZ() / result.W;
	}

	float Matrix4::Determinant() const
	{
		const float* m = Data();
		float inv[16];
		ComputeCofactors(m, inv);
		return m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	}

	Matrix4 Matrix4::Inverse() const
	{
		const float* m = Data();
		float inv[16];
		ComputeCofactors(m, inv);

		const float determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
		if (Math::Abs(determinant) < Math::Epsilon * Math::Epsilon)
		{
			return Identity;
		}

		const float invDeterminant = 1.0f / determinant;
		Matrix4 result;
		float* out = result.Data();
		for (int i = 0; i < 16; ++i)
		{
			out[i] = inv[i] * invDeterminant;
		}
		return result;
	}

	bool Matrix4::Decompose(Vector3& outTranslation, Quaternion& outRotation, Vector3& outScale) const
	{
		outTranslation = GetTranslation();

		Vector3 right = GetRight();
		Vector3 up = GetUp();
		Vector3 forward = GetForward();
		outScale = { right.Length(), up.Length(), forward.Length() };

		if (outScale.X < Math::Epsilon || outScale.Y < Math::Epsilon || outScale.Z < Math::Epsilon)
		{
			outRotation = Quaternion::Identity;
			return false;
		}

		if (Vector3::Dot(Vector3::Cross(right, up), forward) < 0.0f)
		{
			outScale.X = -outScale.X;
		}

		right /= outScale.X;
		up /= outScale.Y;
		forward /= outScale.Z;

		const Matrix4 rotation(
			right.X, right.Y, right.Z, 0.0f,
			up.X, up.Y, up.Z, 0.0f,
			forward.X, forward.Y, forward.Z, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
		outRotation = Quaternion::FromRotationMatrix(rotation);
		return true;
	}

	Matrix4 Matrix4::Translation(const Vector3& translation)
	{
		return {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			translation.X, translation.Y, translation.Z, 1.0f
		};
	}

	Matrix4 Matrix4::Scale(const Vector3& scale)
	{
		return {
			scale.X, 0.0f, 0.0f, 0.0f,
			0.0f, scale.Y, 0.0f, 0.0f,
			0.0f, 0.0f, scale.Z, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
	}

	Matrix4 Matrix4::Scale(float scale)
	{
		return Scale(Vector3{ scale });
	}

	Matrix4 Matrix4::RotationX(float radians)
	{
		const float c = Math::Cos(radians);
		const float s = Math::Sin(radians);
		return {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, c, s, 0.0f,
			0.0f, -s, c, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
	}

	Matrix4 Matrix4::RotationY(float radians)
	{
		const float c = Math::Cos(radians);
		const float s = Math::Sin(radians);
		return {
			c, 0.0f, -s, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			s, 0.0f, c, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
	}

	Matrix4 Matrix4::RotationZ(float radians)
	{
		const float c = Math::Cos(radians);
		const float s = Math::Sin(radians);
		return {
			c, s, 0.0f, 0.0f,
			-s, c, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
	}

	Matrix4 Matrix4::RotationAxis(const Vector3& axis, float radians)
	{
		return Rotation(Quaternion::FromAxisAngle(axis, radians));
	}

	Matrix4 Matrix4::Rotation(const Quaternion& rotation)
	{
		const Quaternion q = rotation.Normalized();
		const float xx = q.X * q.X;
		const float yy = q.Y * q.Y;
		const float zz = q.Z * q.Z;
		const float xy = q.X * q.Y;
		const float xz = q.X * q.Z;
		const float yz = q.Y * q.Z;
		const float wx = q.W * q.X;
		const float wy = q.W * q.Y;
		const float wz = q.W * q.Z;

		return {
			1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f,
			2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f,
			2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
	}

	Matrix4 Matrix4::TRS(const Vector3& translation, const Quaternion& rotation, const Vector3& scale)
	{
		Matrix4 result = Rotation(rotation);
		for (int column = 0; column < 3; ++column)
		{
			result.M[0][column] *= scale.X;
			result.M[1][column] *= scale.Y;
			result.M[2][column] *= scale.Z;
		}
		result.SetTranslation(translation);
		return result;
	}

	Matrix4 Matrix4::LookAt(const Vector3& eye, const Vector3& target, const Vector3& up)
	{
		return LookTo(eye, target - eye, up);
	}

	Matrix4 Matrix4::LookTo(const Vector3& eye, const Vector3& direction, const Vector3& up)
	{
		const Vector3 z = direction.Normalized();
		const Vector3 x = Vector3::Cross(up, z).Normalized();
		const Vector3 y = Vector3::Cross(z, x);

		return {
			x.X, y.X, z.X, 0.0f,
			x.Y, y.Y, z.Y, 0.0f,
			x.Z, y.Z, z.Z, 0.0f,
			-Vector3::Dot(x, eye), -Vector3::Dot(y, eye), -Vector3::Dot(z, eye), 1.0f
		};
	}

	Matrix4 Matrix4::Perspective(float fovYRadians, float aspectRatio, float nearZ, float farZ)
	{
		const float yScale = 1.0f / Math::Tan(fovYRadians * 0.5f);
		const float xScale = yScale / aspectRatio;
		const float range = farZ / (farZ - nearZ);

		return {
			xScale, 0.0f, 0.0f, 0.0f,
			0.0f, yScale, 0.0f, 0.0f,
			0.0f, 0.0f, range, 1.0f,
			0.0f, 0.0f, -range * nearZ, 0.0f
		};
	}

	Matrix4 Matrix4::PerspectiveReversedZ(float fovYRadians, float aspectRatio, float nearZ, float farZ)
	{
		const float yScale = 1.0f / Math::Tan(fovYRadians * 0.5f);
		const float xScale = yScale / aspectRatio;

		return {
			xScale, 0.0f, 0.0f, 0.0f,
			0.0f, yScale, 0.0f, 0.0f,
			0.0f, 0.0f, nearZ / (nearZ - farZ), 1.0f,
			0.0f, 0.0f, farZ * nearZ / (farZ - nearZ), 0.0f
		};
	}

	Matrix4 Matrix4::Orthographic(float width, float height, float nearZ, float farZ)
	{
		const float range = 1.0f / (farZ - nearZ);
		return {
			2.0f / width, 0.0f, 0.0f, 0.0f,
			0.0f, 2.0f / height, 0.0f, 0.0f,
			0.0f, 0.0f, range, 0.0f,
			0.0f, 0.0f, -range * nearZ, 1.0f
		};
	}

	Matrix4 Matrix4::OrthographicOffCenter(float left, float right, float bottom, float top, float nearZ, float farZ)
	{
		const float range = 1.0f / (farZ - nearZ);
		return {
			2.0f / (right - left), 0.0f, 0.0f, 0.0f,
			0.0f, 2.0f / (top - bottom), 0.0f, 0.0f,
			0.0f, 0.0f, range, 0.0f,
			(left + right) / (left - right), (top + bottom) / (bottom - top), -range * nearZ, 1.0f
		};
	}

	bool Matrix4::IsNearlyEqual(const Matrix4& a, const Matrix4& b, float tolerance)
	{
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				if (!Math::IsNearlyEqual(a.M[row][column], b.M[row][column], tolerance))
				{
					return false;
				}
			}
		}
		return true;
	}
}
