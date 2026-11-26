#include "Snapping/SnapMath.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace Canape;

namespace SnapMath
{
    namespace
    {
        Vector3 Unproject(const Matrix4& inverseViewProjection, float ndcX, float ndcY, float depth)
        {
            return Vector3::FromXM(DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(ndcX, ndcY, depth, 1.0f),
                inverseViewProjection.ToXM()));
        }
    }

    Ray ScreenRay(const Matrix4& viewProjection, const ScreenRect& rect, float screenX, float screenY)
    {
        const float ndcX = (screenX - rect.x) / std::max(rect.width, 1.0f) * 2.0f - 1.0f;
        const float ndcY = 1.0f - (screenY - rect.y) / std::max(rect.height, 1.0f) * 2.0f;
        const Matrix4 inverse = viewProjection.Inverse();
        const Vector3 nearPoint = Unproject(inverse, ndcX, ndcY, 0.0f);
        const Vector3 farPoint = Unproject(inverse, ndcX, ndcY, 1.0f);
        return { nearPoint, (farPoint - nearPoint).Normalized() };
    }

    std::optional<Vector2> WorldToScreen(const Matrix4& viewProjection, const ScreenRect& rect, const Vector3& world)
    {
        const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(DirectX::XMVectorSet(world.X, world.Y, world.Z, 1.0f),
            viewProjection.ToXM());
        const float w = DirectX::XMVectorGetW(clip);
        if (w <= 1e-5f)
        {
            return std::nullopt;
        }
        const float ndcX = DirectX::XMVectorGetX(clip) / w;
        const float ndcY = DirectX::XMVectorGetY(clip) / w;
        return Vector2{ rect.x + (ndcX * 0.5f + 0.5f) * rect.width, rect.y + (0.5f - ndcY * 0.5f) * rect.height };
    }

    std::optional<float> RayTriangle(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c)
    {
        constexpr float kEpsilon = 1e-8f;
        const Vector3 edge1 = b - a;
        const Vector3 edge2 = c - a;
        const Vector3 p = Vector3::Cross(ray.direction, edge2);
        const float determinant = Vector3::Dot(edge1, p);
        if (std::abs(determinant) < kEpsilon)
        {
            return std::nullopt;
        }
        const float inverse = 1.0f / determinant;
        const Vector3 s = ray.origin - a;
        const float u = Vector3::Dot(s, p) * inverse;
        if (u < 0.0f || u > 1.0f)
        {
            return std::nullopt;
        }
        const Vector3 q = Vector3::Cross(s, edge1);
        const float v = Vector3::Dot(ray.direction, q) * inverse;
        if (v < 0.0f || u + v > 1.0f)
        {
            return std::nullopt;
        }
        const float t = Vector3::Dot(edge2, q) * inverse;
        if (t < 0.0f)
        {
            return std::nullopt;
        }
        return t;
    }

    std::optional<float> RayAabb(const Ray& ray, const Vector3& min, const Vector3& max)
    {
        float tMin = 0.0f;
        float tMax = FLT_MAX;
        const float origin[3] = { ray.origin.X, ray.origin.Y, ray.origin.Z };
        const float direction[3] = { ray.direction.X, ray.direction.Y, ray.direction.Z };
        const float lo[3] = { min.X, min.Y, min.Z };
        const float hi[3] = { max.X, max.Y, max.Z };
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(direction[axis]) < 1e-12f)
            {
                if (origin[axis] < lo[axis] || origin[axis] > hi[axis])
                {
                    return std::nullopt;
                }
                continue;
            }
            const float inverse = 1.0f / direction[axis];
            float t0 = (lo[axis] - origin[axis]) * inverse;
            float t1 = (hi[axis] - origin[axis]) * inverse;
            if (t0 > t1)
            {
                std::swap(t0, t1);
            }
            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
            if (tMin > tMax)
            {
                return std::nullopt;
            }
        }
        return tMin;
    }

    Vector3 ClosestPointOnSegment(const Vector3& point, const Vector3& a, const Vector3& b)
    {
        const Vector3 ab = b - a;
        const float lengthSquared = Vector3::Dot(ab, ab);
        if (lengthSquared < 1e-12f)
        {
            return a;
        }
        const float t = std::clamp(Vector3::Dot(point - a, ab) / lengthSquared, 0.0f, 1.0f);
        return a + ab * t;
    }

    float DistanceToSegment2D(const Vector2& point, const Vector2& a, const Vector2& b, float* t)
    {
        const Vector2 ab = b - a;
        const float lengthSquared = ab.X * ab.X + ab.Y * ab.Y;
        float s = 0.0f;
        if (lengthSquared > 1e-12f)
        {
            const Vector2 ap = point - a;
            s = std::clamp((ap.X * ab.X + ap.Y * ab.Y) / lengthSquared, 0.0f, 1.0f);
        }
        if (t)
        {
            *t = s;
        }
        return (point - (a + ab * s)).Length();
    }

    float SnapToGrid(float value, float cellSize)
    {
        if (cellSize <= 0.0f)
        {
            return value;
        }
        return std::round(value / cellSize) * cellSize;
    }

    Vector3 SnapMovedAxesToGrid(const Vector3& position, const Vector3& dragStart, float cellSize)
    {
        constexpr float kMoved = 1e-5f;
        return {
            std::abs(position.X - dragStart.X) > kMoved ? SnapToGrid(position.X, cellSize) : position.X,
            std::abs(position.Y - dragStart.Y) > kMoved ? SnapToGrid(position.Y, cellSize) : position.Y,
            std::abs(position.Z - dragStart.Z) > kMoved ? SnapToGrid(position.Z, cellSize) : position.Z,
        };
    }

    float MinExtentAlong(const std::vector<Vector3>& points, const Vector3& pivot, const Vector3& normal)
    {
        if (points.empty())
        {
            return 0.0f;
        }
        float minimum = FLT_MAX;
        for (const Vector3& point : points)
        {
            minimum = std::min(minimum, Vector3::Dot(point - pivot, normal));
        }
        return minimum;
    }

    Quaternion RotationBetween(const Vector3& from, const Vector3& to)
    {
        const float cosine = std::clamp(Vector3::Dot(from, to), -1.0f, 1.0f);
        if (cosine > 1.0f - 1e-6f)
        {
            return Quaternion::Identity;
        }
        Vector3 axis = Vector3::Cross(from, to);
        if (cosine < -1.0f + 1e-6f)
        {
            // Opposite vectors: any axis perpendicular to `from` works.
            axis = Vector3::Cross(from, std::abs(from.X) < 0.9f ? Vector3::Right : Vector3::Up);
        }
        return Quaternion::FromXM(DirectX::XMQuaternionRotationAxis(axis.Normalized().ToXM(), std::acos(cosine)));
    }

    Vector3 SkinPosition(const Vector3& position, const uint16_t joints[4], const float weights[4],
        const std::vector<Matrix4>& skinMatrices)
    {
        Vector3 result{};
        float total = 0.0f;
        for (int influence = 0; influence < 4; ++influence)
        {
            const float weight = weights[influence];
            if (weight <= 0.0f || joints[influence] >= skinMatrices.size())
            {
                continue;
            }
            result += skinMatrices[joints[influence]].TransformPoint(position) * weight;
            total += weight;
        }
        return total > 1e-6f ? result / total : position;
    }
}
