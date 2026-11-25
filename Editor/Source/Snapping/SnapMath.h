#pragma once

#include "Math/Matrix4.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"

#include <cstdint>
#include <optional>
#include <vector>

// Pure geometry used by scene-view snapping. No ImGui or scene dependencies so it can be validated on its own.
namespace SnapMath
{
    struct Ray
    {
        Canape::Vector3 origin;
        Canape::Vector3 direction; // normalized
    };

    struct ScreenRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 1.0f;
        float height = 1.0f;
    };

    // World ray through a screen point (absolute pixels) of an image that shows `viewProjection` (row-vector, D3D depth 0..1).
    Ray ScreenRay(const Canape::Matrix4& viewProjection, const ScreenRect& rect, float screenX, float screenY);

    // Projects a world point into the image. Returns nothing when the point is behind the camera.
    std::optional<Canape::Vector2> WorldToScreen(const Canape::Matrix4& viewProjection, const ScreenRect& rect,
        const Canape::Vector3& world);

    // Möller–Trumbore, both faces. Returns the ray distance of the hit.
    std::optional<float> RayTriangle(const Ray& ray, const Canape::Vector3& a, const Canape::Vector3& b, const Canape::Vector3& c);

    // Slab test against an axis-aligned box. Returns the entry distance (0 when the origin is inside).
    std::optional<float> RayAabb(const Ray& ray, const Canape::Vector3& min, const Canape::Vector3& max);

    Canape::Vector3 ClosestPointOnSegment(const Canape::Vector3& point, const Canape::Vector3& a, const Canape::Vector3& b);
    float DistanceToSegment2D(const Canape::Vector2& point, const Canape::Vector2& a, const Canape::Vector2& b, float* t = nullptr);

    float SnapToGrid(float value, float cellSize);

    // Rounds to the world grid only the axes that moved away from `dragStart`, so an axis-constrained drag
    // never pulls the other axes of an off-grid object onto the grid.
    Canape::Vector3 SnapMovedAxesToGrid(const Canape::Vector3& position, const Canape::Vector3& dragStart, float cellSize);

    // Smallest signed distance of the points along `normal`, measured from `pivot`. Used to rest an object on a surface.
    float MinExtentAlong(const std::vector<Canape::Vector3>& points, const Canape::Vector3& pivot, const Canape::Vector3& normal);

    // Shortest-arc rotation that turns unit vector `from` onto unit vector `to`.
    Canape::Quaternion RotationBetween(const Canape::Vector3& from, const Canape::Vector3& to);

    // Linear-blend skinning of one position with up to four joints.
    Canape::Vector3 SkinPosition(const Canape::Vector3& position, const uint16_t joints[4], const float weights[4],
        const std::vector<Canape::Matrix4>& skinMatrices);
}
