#include "Math/Quaternion.h"
#include "Math/Matrix4.h"

namespace Canape
{
	Quaternion Quaternion::FromAxisAngle(const Vector3& axis, float radians)
	{
		const Vector3 n = axis.Normalized();
		const float half = radians * 0.5f;
		const float s = Math::Sin(half);
		return { n.X * s, n.Y * s, n.Z * s, Math::Cos(half) };
	}

	Quaternion Quaternion::FromRotationMatrix(const Matrix4& matrix)
	{
		const auto& m = matrix.M;
		const float trace = m[0][0] + m[1][1] + m[2][2];

		Quaternion q;
		if (trace > 0.0f)
		{
			const float s = Math::Sqrt(trace + 1.0f) * 2.0f;
			q.W = 0.25f * s;
			q.X = (m[1][2] - m[2][1]) / s;
			q.Y = (m[2][0] - m[0][2]) / s;
			q.Z = (m[0][1] - m[1][0]) / s;
		}
		else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
		{
			const float s = Math::Sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
			q.W = (m[1][2] - m[2][1]) / s;
			q.X = 0.25f * s;
			q.Y = (m[1][0] + m[0][1]) / s;
			q.Z = (m[2][0] + m[0][2]) / s;
		}
		else if (m[1][1] > m[2][2])
		{
			const float s = Math::Sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
			q.W = (m[2][0] - m[0][2]) / s;
			q.X = (m[1][0] + m[0][1]) / s;
			q.Y = 0.25f * s;
			q.Z = (m[2][1] + m[1][2]) / s;
		}
		else
		{
			const float s = Math::Sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
			q.W = (m[0][1] - m[1][0]) / s;
			q.X = (m[2][0] + m[0][2]) / s;
			q.Y = (m[2][1] + m[1][2]) / s;
			q.Z = 0.25f * s;
		}
		return q.Normalized();
	}

	Quaternion Quaternion::LookRotation(const Vector3& forward, const Vector3& up)
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
		r.Normalize();
		const Vector3 u = Vector3::Cross(f, r);

		const Matrix4 basis(
			r.X, r.Y, r.Z, 0.0f,
			u.X, u.Y, u.Z, 0.0f,
			f.X, f.Y, f.Z, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
		return FromRotationMatrix(basis);
	}

	Quaternion Quaternion::FromToRotation(const Vector3& from, const Vector3& to)
	{
		const Vector3 f = from.Normalized();
		const Vector3 t = to.Normalized();
		const float d = Vector3::Dot(f, t);

		if (d >= 1.0f - Math::Epsilon)
		{
			return Identity;
		}

		if (d <= -1.0f + Math::Epsilon)
		{
			Vector3 axis = Vector3::Cross(Vector3::Right, f);
			if (axis.IsNearlyZero())
			{
				axis = Vector3::Cross(Vector3::Up, f);
			}
			return FromAxisAngle(axis, Math::Pi);
		}

		const Vector3 c = Vector3::Cross(f, t);
		const float s = Math::Sqrt((1.0f + d) * 2.0f);
		const float invS = 1.0f / s;
		return Quaternion{ c.X * invS, c.Y * invS, c.Z * invS, s * 0.5f }.Normalized();
	}

	float Quaternion::Angle(const Quaternion& a, const Quaternion& b)
	{
		const float d = Math::Min(Math::Abs(Dot(a, b)), 1.0f);
		return 2.0f * Math::Acos(d);
	}

	Quaternion Quaternion::Slerp(const Quaternion& a, const Quaternion& b, float t)
	{
		Quaternion end = b;
		float cosTheta = Dot(a, b);
		if (cosTheta < 0.0f)
		{
			end = { -b.X, -b.Y, -b.Z, -b.W };
			cosTheta = -cosTheta;
		}

		if (cosTheta > 0.9995f)
		{
			return Quaternion{
				Math::Lerp(a.X, end.X, t),
				Math::Lerp(a.Y, end.Y, t),
				Math::Lerp(a.Z, end.Z, t),
				Math::Lerp(a.W, end.W, t) }.Normalized();
		}

		const float theta = Math::Acos(cosTheta);
		const float sinTheta = Math::Sin(theta);
		const float wa = Math::Sin((1.0f - t) * theta) / sinTheta;
		const float wb = Math::Sin(t * theta) / sinTheta;
		return {
			a.X * wa + end.X * wb,
			a.Y * wa + end.Y * wb,
			a.Z * wa + end.Z * wb,
			a.W * wa + end.W * wb
		};
	}

	Quaternion Quaternion::Nlerp(const Quaternion& a, const Quaternion& b, float t)
	{
		const float sign = Dot(a, b) < 0.0f ? -1.0f : 1.0f;
		return Quaternion{
			Math::Lerp(a.X, b.X * sign, t),
			Math::Lerp(a.Y, b.Y * sign, t),
			Math::Lerp(a.Z, b.Z * sign, t),
			Math::Lerp(a.W, b.W * sign, t) }.Normalized();
	}

	Quaternion Quaternion::RotateTowards(const Quaternion& from, const Quaternion& to, float maxRadiansDelta)
	{
		const float angle = Angle(from, to);
		if (angle < Math::Epsilon)
		{
			return to;
		}
		return Slerp(from, to, Math::Min(1.0f, maxRadiansDelta / angle));
	}

	bool Quaternion::IsNearlyEqual(const Quaternion& a, const Quaternion& b, float tolerance)
	{
		return Math::Abs(Dot(a, b)) >= 1.0f - tolerance;
	}

	Quaternion Quaternion::Normalized() const
	{
		const float length = Length();
		if (length < Math::Epsilon)
		{
			return Identity;
		}
		const float inv = 1.0f / length;
		return { X * inv, Y * inv, Z * inv, W * inv };
	}

	void Quaternion::ToAxisAngle(Vector3& outAxis, float& outRadians) const
	{
		const Quaternion q = W < 0.0f ? Quaternion{ -X, -Y, -Z, -W }.Normalized() : Normalized();
		outRadians = 2.0f * Math::Acos(q.W);

		const float s = Math::Sqrt(Math::Max(0.0f, 1.0f - q.W * q.W));
		if (s < Math::Epsilon)
		{
			outAxis = Vector3::Right;
			return;
		}
		outAxis = Vector3{ q.X, q.Y, q.Z } / s;
	}

	Matrix4 Quaternion::ToMatrix() const
	{
		return Matrix4::Rotation(*this);
	}
}
