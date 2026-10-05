#include "Math/Geometry.h"

namespace Canape
{
	Plane Plane::FromPointNormal(const Vector3& point, const Vector3& normal)
	{
		const Vector3 n = normal.Normalized();
		return { n, -Vector3::Dot(n, point) };
	}

	Plane Plane::FromPoints(const Vector3& a, const Vector3& b, const Vector3& c)
	{
		return FromPointNormal(a, Vector3::Cross(b - a, c - a));
	}

	Plane Plane::Normalized() const
	{
		const float length = Normal.Length();
		if (length < Math::Epsilon)
		{
			return *this;
		}
		return { Normal / length, D / length };
	}

	std::array<Vector3, 8> AABB::GetCorners() const
	{
		return {
			Vector3{ Min.X, Min.Y, Min.Z },
			Vector3{ Max.X, Min.Y, Min.Z },
			Vector3{ Min.X, Max.Y, Min.Z },
			Vector3{ Max.X, Max.Y, Min.Z },
			Vector3{ Min.X, Min.Y, Max.Z },
			Vector3{ Max.X, Min.Y, Max.Z },
			Vector3{ Min.X, Max.Y, Max.Z },
			Vector3{ Max.X, Max.Y, Max.Z }
		};
	}

	AABB AABB::Transformed(const Matrix4& matrix) const
	{
		const Vector3 center = matrix.TransformPoint(GetCenter());
		const Vector3 extents = GetExtents();

		Vector3 newExtents;
		for (int column = 0; column < 3; ++column)
		{
			newExtents[column] =
				Math::Abs(matrix.M[0][column]) * extents.X +
				Math::Abs(matrix.M[1][column]) * extents.Y +
				Math::Abs(matrix.M[2][column]) * extents.Z;
		}
		return FromCenterExtents(center, newExtents);
	}

	Frustum Frustum::FromViewProjection(const Matrix4& viewProjection)
	{
		const Vector4 c0 = viewProjection.GetColumn(0);
		const Vector4 c1 = viewProjection.GetColumn(1);
		const Vector4 c2 = viewProjection.GetColumn(2);
		const Vector4 c3 = viewProjection.GetColumn(3);

		const auto makePlane = [](const Vector4& v)
		{
			return Plane{ v.XYZ(), v.W }.Normalized();
		};

		Frustum frustum;
		frustum.Planes[Left] = makePlane(c3 + c0);
		frustum.Planes[Right] = makePlane(c3 - c0);
		frustum.Planes[Bottom] = makePlane(c3 + c1);
		frustum.Planes[Top] = makePlane(c3 - c1);
		frustum.Planes[Near] = makePlane(c2);
		frustum.Planes[Far] = makePlane(c3 - c2);
		return frustum;
	}

	bool Frustum::Contains(const Vector3& point) const
	{
		for (const Plane& plane : Planes)
		{
			if (plane.SignedDistance(point) < 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	bool Frustum::Intersects(const Sphere& sphere) const
	{
		for (const Plane& plane : Planes)
		{
			if (plane.SignedDistance(sphere.Center) < -sphere.Radius)
			{
				return false;
			}
		}
		return true;
	}

	bool Frustum::Intersects(const AABB& box) const
	{
		for (const Plane& plane : Planes)
		{
			const Vector3 positive{
				plane.Normal.X >= 0.0f ? box.Max.X : box.Min.X,
				plane.Normal.Y >= 0.0f ? box.Max.Y : box.Min.Y,
				plane.Normal.Z >= 0.0f ? box.Max.Z : box.Min.Z
			};

			if (plane.SignedDistance(positive) < 0.0f)
			{
				return false;
			}
		}
		return true;
	}
}
