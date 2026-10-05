#pragma once

#include "Math/Geometry.h"

#include <optional>

namespace Canape::Intersection
{
	std::optional<float> RayPlane(const Ray& ray, const Plane& plane);
	std::optional<float> RaySphere(const Ray& ray, const Sphere& sphere);
	std::optional<float> RayAABB(const Ray& ray, const AABB& box);
	std::optional<float> RayTriangle(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c, bool cullBackFace = false);

	bool SphereSphere(const Sphere& a, const Sphere& b);
	bool SphereAABB(const Sphere& sphere, const AABB& box);
	bool SpherePlane(const Sphere& sphere, const Plane& plane);
	bool AABBAABB(const AABB& a, const AABB& b);
	bool AABBPlane(const AABB& box, const Plane& plane);

	Vector3 ClosestPointOnSegment(const Vector3& point, const Vector3& a, const Vector3& b);
	Vector3 ClosestPointOnTriangle(const Vector3& point, const Vector3& a, const Vector3& b, const Vector3& c);
	void ClosestPointsBetweenSegments(const Vector3& p0, const Vector3& p1, const Vector3& q0, const Vector3& q1, Vector3& outOnP, Vector3& outOnQ);
	float DistancePointSegment(const Vector3& point, const Vector3& a, const Vector3& b);
}
