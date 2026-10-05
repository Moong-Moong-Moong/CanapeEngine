#pragma once

#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"

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

		constexpr Matrix4(
			float m00, float m01, float m02, float m03,
			float m10, float m11, float m12, float m13,
			float m20, float m21, float m22, float m23,
			float m30, float m31, float m32, float m33)
			: M{ { m00, m01, m02, m03 },
				 { m10, m11, m12, m13 },
				 { m20, m21, m22, m23 },
				 { m30, m31, m32, m33 } }
		{
		}

		constexpr Matrix4(const Vector4& row0, const Vector4& row1, const Vector4& row2, const Vector4& row3)
			: M{ { row0.X, row0.Y, row0.Z, row0.W },
				 { row1.X, row1.Y, row1.Z, row1.W },
				 { row2.X, row2.Y, row2.Z, row2.W },
				 { row3.X, row3.Y, row3.Z, row3.W } }
		{
		}

		static const Matrix4 Identity;

		float* Data() { return &M[0][0]; }
		const float* Data() const { return &M[0][0]; }

		constexpr Vector4 GetRow(int row) const { return { M[row][0], M[row][1], M[row][2], M[row][3] }; }
		constexpr Vector4 GetColumn(int column) const { return { M[0][column], M[1][column], M[2][column], M[3][column] }; }

		constexpr void SetRow(int row, const Vector4& v)
		{
			M[row][0] = v.X;
			M[row][1] = v.Y;
			M[row][2] = v.Z;
			M[row][3] = v.W;
		}

		constexpr Vector3 GetRight() const { return { M[0][0], M[0][1], M[0][2] }; }
		constexpr Vector3 GetUp() const { return { M[1][0], M[1][1], M[1][2] }; }
		constexpr Vector3 GetForward() const { return { M[2][0], M[2][1], M[2][2] }; }
		constexpr Vector3 GetTranslation() const { return { M[3][0], M[3][1], M[3][2] }; }

		constexpr void SetTranslation(const Vector3& translation)
		{
			M[3][0] = translation.X;
			M[3][1] = translation.Y;
			M[3][2] = translation.Z;
		}

		Vector3 GetScale() const { return { GetRight().Length(), GetUp().Length(), GetForward().Length() }; }

		constexpr Matrix4 operator*(const Matrix4& rhs) const
		{
			Matrix4 result(
				0.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 0.0f);

			for (int row = 0; row < 4; ++row)
			{
				for (int column = 0; column < 4; ++column)
				{
					result.M[row][column] =
						M[row][0] * rhs.M[0][column] +
						M[row][1] * rhs.M[1][column] +
						M[row][2] * rhs.M[2][column] +
						M[row][3] * rhs.M[3][column];
				}
			}
			return result;
		}

		constexpr Matrix4& operator*=(const Matrix4& rhs) { *this = *this * rhs; return *this; }

		constexpr bool operator==(const Matrix4& rhs) const
		{
			for (int row = 0; row < 4; ++row)
			{
				for (int column = 0; column < 4; ++column)
				{
					if (M[row][column] != rhs.M[row][column])
					{
						return false;
					}
				}
			}
			return true;
		}

		constexpr Vector4 Transform(const Vector4& v) const
		{
			return {
				v.X * M[0][0] + v.Y * M[1][0] + v.Z * M[2][0] + v.W * M[3][0],
				v.X * M[0][1] + v.Y * M[1][1] + v.Z * M[2][1] + v.W * M[3][1],
				v.X * M[0][2] + v.Y * M[1][2] + v.Z * M[2][2] + v.W * M[3][2],
				v.X * M[0][3] + v.Y * M[1][3] + v.Z * M[2][3] + v.W * M[3][3]
			};
		}

		constexpr Vector3 TransformPoint(const Vector3& p) const
		{
			return {
				p.X * M[0][0] + p.Y * M[1][0] + p.Z * M[2][0] + M[3][0],
				p.X * M[0][1] + p.Y * M[1][1] + p.Z * M[2][1] + M[3][1],
				p.X * M[0][2] + p.Y * M[1][2] + p.Z * M[2][2] + M[3][2]
			};
		}

		constexpr Vector3 TransformVector(const Vector3& v) const
		{
			return {
				v.X * M[0][0] + v.Y * M[1][0] + v.Z * M[2][0],
				v.X * M[0][1] + v.Y * M[1][1] + v.Z * M[2][1],
				v.X * M[0][2] + v.Y * M[1][2] + v.Z * M[2][2]
			};
		}

		Vector3 ProjectPoint(const Vector3& p) const;

		constexpr Matrix4 Transposed() const
		{
			return {
				M[0][0], M[1][0], M[2][0], M[3][0],
				M[0][1], M[1][1], M[2][1], M[3][1],
				M[0][2], M[1][2], M[2][2], M[3][2],
				M[0][3], M[1][3], M[2][3], M[3][3]
			};
		}

		float Determinant() const;
		Matrix4 Inverse() const;
		bool Decompose(Vector3& outTranslation, Quaternion& outRotation, Vector3& outScale) const;

		static Matrix4 Translation(const Vector3& translation);
		static Matrix4 Scale(const Vector3& scale);
		static Matrix4 Scale(float scale);
		static Matrix4 RotationX(float radians);
		static Matrix4 RotationY(float radians);
		static Matrix4 RotationZ(float radians);
		static Matrix4 RotationAxis(const Vector3& axis, float radians);
		static Matrix4 Rotation(const Quaternion& rotation);
		static Matrix4 TRS(const Vector3& translation, const Quaternion& rotation, const Vector3& scale);

		static Matrix4 LookAt(const Vector3& eye, const Vector3& target, const Vector3& up = Vector3::Up);
		static Matrix4 LookTo(const Vector3& eye, const Vector3& direction, const Vector3& up = Vector3::Up);

		static Matrix4 Perspective(float fovYRadians, float aspectRatio, float nearZ, float farZ);
		static Matrix4 PerspectiveReversedZ(float fovYRadians, float aspectRatio, float nearZ, float farZ);
		static Matrix4 Orthographic(float width, float height, float nearZ, float farZ);
		static Matrix4 OrthographicOffCenter(float left, float right, float bottom, float top, float nearZ, float farZ);

		static bool IsNearlyEqual(const Matrix4& a, const Matrix4& b, float tolerance = Math::SmallNumber);
	};

	inline constexpr Matrix4 Matrix4::Identity{};

	constexpr Vector4 operator*(const Vector4& v, const Matrix4& m) { return m.Transform(v); }
}
