#pragma once

#include "Math/Matrix4.h"
#include "Math/Vector3.h"

#include <array>

namespace Canape
{
	struct Ray
	{
		Vector3 Origin = Vector3::Zero;
		Vector3 Direction = Vector3::Forward;

		constexpr Ray() = default;
		Ray(const Vector3& origin, const Vector3& direction) : Origin(origin), Direction(direction.Normalized()) {}

		constexpr Vector3 GetPoint(float distance) const { return Origin + Direction * distance; }
	};

	struct Plane
	{
		Vector3 Normal = Vector3::Up;
		float D = 0.0f;

		constexpr Plane() = default;
		constexpr Plane(const Vector3& normal, float d) : Normal(normal), D(d) {}

		static Plane FromPointNormal(const Vector3& point, const Vector3& normal);
		static Plane FromPoints(const Vector3& a, const Vector3& b, const Vector3& c);

		constexpr float SignedDistance(const Vector3& point) const { return Vector3::Dot(Normal, point) + D; }
		constexpr Vector3 ClosestPoint(const Vector3& point) const { return point - Normal * SignedDistance(point); }
		constexpr bool IsInFront(const Vector3& point) const { return SignedDistance(point) > 0.0f; }
		constexpr Plane Flipped() const { return { -Normal, -D }; }

		Plane Normalized() const;
	};

	struct Sphere
	{
		Vector3 Center = Vector3::Zero;
		float Radius = 0.0f;

		constexpr Sphere() = default;
		constexpr Sphere(const Vector3& center, float radius) : Center(center), Radius(radius) {}

		constexpr bool Contains(const Vector3& point) const { return Vector3::DistanceSquared(Center, point) <= Radius * Radius; }
	};

	struct AABB
	{
		Vector3 Min = Vector3{ Math::MaxFloat };
		Vector3 Max = Vector3{ -Math::MaxFloat };

		constexpr AABB() = default;
		constexpr AABB(const Vector3& min, const Vector3& max) : Min(min), Max(max) {}

		static constexpr AABB FromCenterExtents(const Vector3& center, const Vector3& extents)
		{
			return { center - extents, center + extents };
		}

		constexpr bool IsValid() const { return Min.X <= Max.X && Min.Y <= Max.Y && Min.Z <= Max.Z; }

		constexpr Vector3 GetCenter() const { return (Min + Max) * 0.5f; }
		constexpr Vector3 GetExtents() const { return (Max - Min) * 0.5f; }
		constexpr Vector3 GetSize() const { return Max - Min; }

		constexpr bool Contains(const Vector3& point) const
		{
			return point.X >= Min.X && point.X <= Max.X
				&& point.Y >= Min.Y && point.Y <= Max.Y
				&& point.Z >= Min.Z && point.Z <= Max.Z;
		}

		constexpr bool Contains(const AABB& other) const { return Contains(other.Min) && Contains(other.Max); }

		constexpr void Encapsulate(const Vector3& point)
		{
			Min = Vector3::Min(Min, point);
			Max = Vector3::Max(Max, point);
		}

		constexpr void Encapsulate(const AABB& other)
		{
			Min = Vector3::Min(Min, other.Min);
			Max = Vector3::Max(Max, other.Max);
		}

		constexpr AABB Expanded(float amount) const { return { Min - Vector3{ amount }, Max + Vector3{ amount } }; }

		constexpr Vector3 ClosestPoint(const Vector3& point) const { return Vector3::Min(Vector3::Max(point, Min), Max); }

		std::array<Vector3, 8> GetCorners() const;
		AABB Transformed(const Matrix4& matrix) const;
	};

	struct Frustum
	{
		enum PlaneIndex
		{
			Left,
			Right,
			Bottom,
			Top,
			Near,
			Far,
			Count
		};

		std::array<Plane, Count> Planes;

		static Frustum FromViewProjection(const Matrix4& viewProjection);

		bool Contains(const Vector3& point) const;
		bool Intersects(const Sphere& sphere) const;
		bool Intersects(const AABB& box) const;
	};
}
