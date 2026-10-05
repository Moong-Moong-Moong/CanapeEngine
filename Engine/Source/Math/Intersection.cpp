#include "Math/Intersection.h"

namespace Canape::Intersection
{
	std::optional<float> RayPlane(const Ray& ray, const Plane& plane)
	{
		const float denominator = Vector3::Dot(plane.Normal, ray.Direction);
		if (Math::Abs(denominator) < Math::Epsilon)
		{
			return std::nullopt;
		}

		const float t = -plane.SignedDistance(ray.Origin) / denominator;
		if (t < 0.0f)
		{
			return std::nullopt;
		}
		return t;
	}

	std::optional<float> RaySphere(const Ray& ray, const Sphere& sphere)
	{
		const Vector3 m = ray.Origin - sphere.Center;
		const float b = Vector3::Dot(m, ray.Direction);
		const float c = m.LengthSquared() - sphere.Radius * sphere.Radius;

		if (c > 0.0f && b > 0.0f)
		{
			return std::nullopt;
		}

		const float discriminant = b * b - c;
		if (discriminant < 0.0f)
		{
			return std::nullopt;
		}

		const float t = -b - Math::Sqrt(discriminant);
		return Math::Max(t, 0.0f);
	}

	std::optional<float> RayAABB(const Ray& ray, const AABB& box)
	{
		float tMin = 0.0f;
		float tMax = Math::MaxFloat;

		for (int axis = 0; axis < 3; ++axis)
		{
			const float origin = ray.Origin[axis];
			const float direction = ray.Direction[axis];
			const float min = box.Min[axis];
			const float max = box.Max[axis];

			if (Math::Abs(direction) < Math::Epsilon)
			{
				if (origin < min || origin > max)
				{
					return std::nullopt;
				}
				continue;
			}

			const float invDirection = 1.0f / direction;
			float t1 = (min - origin) * invDirection;
			float t2 = (max - origin) * invDirection;
			if (t1 > t2)
			{
				const float temp = t1;
				t1 = t2;
				t2 = temp;
			}

			tMin = Math::Max(tMin, t1);
			tMax = Math::Min(tMax, t2);
			if (tMin > tMax)
			{
				return std::nullopt;
			}
		}
		return tMin;
	}

	std::optional<float> RayTriangle(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c, bool cullBackFace)
	{
		const Vector3 edge1 = b - a;
		const Vector3 edge2 = c - a;
		const Vector3 p = Vector3::Cross(ray.Direction, edge2);
		const float determinant = Vector3::Dot(edge1, p);

		if (cullBackFace ? determinant < Math::Epsilon : Math::Abs(determinant) < Math::Epsilon)
		{
			return std::nullopt;
		}

		const float invDeterminant = 1.0f / determinant;
		const Vector3 s = ray.Origin - a;
		const float u = Vector3::Dot(s, p) * invDeterminant;
		if (u < 0.0f || u > 1.0f)
		{
			return std::nullopt;
		}

		const Vector3 q = Vector3::Cross(s, edge1);
		const float v = Vector3::Dot(ray.Direction, q) * invDeterminant;
		if (v < 0.0f || u + v > 1.0f)
		{
			return std::nullopt;
		}

		const float t = Vector3::Dot(edge2, q) * invDeterminant;
		if (t < 0.0f)
		{
			return std::nullopt;
		}
		return t;
	}

	bool SphereSphere(const Sphere& a, const Sphere& b)
	{
		const float radius = a.Radius + b.Radius;
		return Vector3::DistanceSquared(a.Center, b.Center) <= radius * radius;
	}

	bool SphereAABB(const Sphere& sphere, const AABB& box)
	{
		return Vector3::DistanceSquared(box.ClosestPoint(sphere.Center), sphere.Center) <= sphere.Radius * sphere.Radius;
	}

	bool SpherePlane(const Sphere& sphere, const Plane& plane)
	{
		return Math::Abs(plane.SignedDistance(sphere.Center)) <= sphere.Radius;
	}

	bool AABBAABB(const AABB& a, const AABB& b)
	{
		return a.Min.X <= b.Max.X && a.Max.X >= b.Min.X
			&& a.Min.Y <= b.Max.Y && a.Max.Y >= b.Min.Y
			&& a.Min.Z <= b.Max.Z && a.Max.Z >= b.Min.Z;
	}

	bool AABBPlane(const AABB& box, const Plane& plane)
	{
		const Vector3 extents = box.GetExtents();
		const float radius =
			extents.X * Math::Abs(plane.Normal.X) +
			extents.Y * Math::Abs(plane.Normal.Y) +
			extents.Z * Math::Abs(plane.Normal.Z);
		return Math::Abs(plane.SignedDistance(box.GetCenter())) <= radius;
	}

	Vector3 ClosestPointOnSegment(const Vector3& point, const Vector3& a, const Vector3& b)
	{
		const Vector3 ab = b - a;
		const float lengthSquared = ab.LengthSquared();
		if (lengthSquared < Math::Epsilon)
		{
			return a;
		}

		const float t = Math::Saturate(Vector3::Dot(point - a, ab) / lengthSquared);
		return a + ab * t;
	}

	Vector3 ClosestPointOnTriangle(const Vector3& point, const Vector3& a, const Vector3& b, const Vector3& c)
	{
		const Vector3 ab = b - a;
		const Vector3 ac = c - a;
		const Vector3 ap = point - a;

		const float d1 = Vector3::Dot(ab, ap);
		const float d2 = Vector3::Dot(ac, ap);
		if (d1 <= 0.0f && d2 <= 0.0f)
		{
			return a;
		}

		const Vector3 bp = point - b;
		const float d3 = Vector3::Dot(ab, bp);
		const float d4 = Vector3::Dot(ac, bp);
		if (d3 >= 0.0f && d4 <= d3)
		{
			return b;
		}

		const float vc = d1 * d4 - d3 * d2;
		if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
		{
			return a + ab * (d1 / (d1 - d3));
		}

		const Vector3 cp = point - c;
		const float d5 = Vector3::Dot(ab, cp);
		const float d6 = Vector3::Dot(ac, cp);
		if (d6 >= 0.0f && d5 <= d6)
		{
			return c;
		}

		const float vb = d5 * d2 - d1 * d6;
		if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
		{
			return a + ac * (d2 / (d2 - d6));
		}

		const float va = d3 * d6 - d5 * d4;
		if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
		{
			return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
		}

		const float denominator = 1.0f / (va + vb + vc);
		const float v = vb * denominator;
		const float w = vc * denominator;
		return a + ab * v + ac * w;
	}

	void ClosestPointsBetweenSegments(const Vector3& p0, const Vector3& p1, const Vector3& q0, const Vector3& q1, Vector3& outOnP, Vector3& outOnQ)
	{
		const Vector3 d1 = p1 - p0;
		const Vector3 d2 = q1 - q0;
		const Vector3 r = p0 - q0;
		const float a = d1.LengthSquared();
		const float e = d2.LengthSquared();
		const float f = Vector3::Dot(d2, r);

		float s = 0.0f;
		float t = 0.0f;

		if (a <= Math::Epsilon && e <= Math::Epsilon)
		{
			outOnP = p0;
			outOnQ = q0;
			return;
		}

		if (a <= Math::Epsilon)
		{
			t = Math::Saturate(f / e);
		}
		else
		{
			const float c = Vector3::Dot(d1, r);
			if (e <= Math::Epsilon)
			{
				s = Math::Saturate(-c / a);
			}
			else
			{
				const float b = Vector3::Dot(d1, d2);
				const float denominator = a * e - b * b;
				s = denominator != 0.0f ? Math::Saturate((b * f - c * e) / denominator) : 0.0f;
				t = (b * s + f) / e;

				if (t < 0.0f)
				{
					t = 0.0f;
					s = Math::Saturate(-c / a);
				}
				else if (t > 1.0f)
				{
					t = 1.0f;
					s = Math::Saturate((b - c) / a);
				}
			}
		}

		outOnP = p0 + d1 * s;
		outOnQ = q0 + d2 * t;
	}

	float DistancePointSegment(const Vector3& point, const Vector3& a, const Vector3& b)
	{
		return Vector3::Distance(point, ClosestPointOnSegment(point, a, b));
	}
}
