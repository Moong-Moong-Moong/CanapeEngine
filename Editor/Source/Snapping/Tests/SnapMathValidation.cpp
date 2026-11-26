#include "Snapping/SnapMath.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Canape;

namespace
{
    int g_Failures = 0;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::printf("FAIL: %s\n", message);
            ++g_Failures;
        }
    }

    bool Near(float a, float b, float tolerance = 1e-4f)
    {
        return std::abs(a - b) <= tolerance;
    }

    bool Near(const Vector3& a, const Vector3& b, float tolerance = 1e-4f)
    {
        return Near(a.X, b.X, tolerance) && Near(a.Y, b.Y, tolerance) && Near(a.Z, b.Z, tolerance);
    }

    void RayTriangleCases()
    {
        const Vector3 a{ -1.0f, 0.0f, -1.0f };
        const Vector3 b{ 1.0f, 0.0f, -1.0f };
        const Vector3 c{ 0.0f, 0.0f, 1.0f };

        const SnapMath::Ray down{ { 0.0f, 5.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } };
        const auto hit = SnapMath::RayTriangle(down, a, b, c);
        Require(hit && Near(*hit, 5.0f), "ray hits triangle from above at distance 5");

        const SnapMath::Ray up{ { 0.0f, -2.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
        Require(SnapMath::RayTriangle(up, a, b, c) && Near(*SnapMath::RayTriangle(up, a, b, c), 2.0f), "back face is hit too");

        const SnapMath::Ray miss{ { 3.0f, 5.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } };
        Require(!SnapMath::RayTriangle(miss, a, b, c), "ray outside triangle misses");

        const SnapMath::Ray away{ { 0.0f, 5.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
        Require(!SnapMath::RayTriangle(away, a, b, c), "triangle behind ray origin misses");

        const SnapMath::Ray parallel{ { 0.0f, 0.0f, -5.0f }, { 0.0f, 0.0f, 1.0f } };
        Require(!SnapMath::RayTriangle(parallel, a, b, c), "ray in triangle plane misses");

        // Unnormalized direction (local-space rays): t stays the parameter of the original ray.
        const SnapMath::Ray scaled{ { 0.0f, 5.0f, 0.0f }, { 0.0f, -2.0f, 0.0f } };
        const auto scaledHit = SnapMath::RayTriangle(scaled, a, b, c);
        Require(scaledHit && Near(*scaledHit, 2.5f), "unnormalized direction keeps the ray parameter");
    }

    void RayAabbCases()
    {
        const Vector3 min{ -1.0f, -1.0f, -1.0f };
        const Vector3 max{ 1.0f, 1.0f, 1.0f };
        const SnapMath::Ray toward{ { -5.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
        const auto entry = SnapMath::RayAabb(toward, min, max);
        Require(entry && Near(*entry, 4.0f), "ray enters box at distance 4");

        const SnapMath::Ray inside{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
        Require(SnapMath::RayAabb(inside, min, max) && Near(*SnapMath::RayAabb(inside, min, max), 0.0f), "origin inside box gives 0");

        const SnapMath::Ray passing{ { -5.0f, 3.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
        Require(!SnapMath::RayAabb(passing, min, max), "ray above box misses");

        const SnapMath::Ray behind{ { 5.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
        Require(!SnapMath::RayAabb(behind, min, max), "box behind ray misses");
    }

    void SegmentCases()
    {
        const Vector3 a{ 0.0f, 0.0f, 0.0f };
        const Vector3 b{ 10.0f, 0.0f, 0.0f };
        Require(Near(SnapMath::ClosestPointOnSegment({ 4.0f, 3.0f, 0.0f }, a, b), { 4.0f, 0.0f, 0.0f }), "projects inside segment");
        Require(Near(SnapMath::ClosestPointOnSegment({ -4.0f, 3.0f, 0.0f }, a, b), a), "clamps before start");
        Require(Near(SnapMath::ClosestPointOnSegment({ 14.0f, 3.0f, 0.0f }, a, b), b), "clamps after end");
        Require(Near(SnapMath::ClosestPointOnSegment({ 1.0f, 1.0f, 1.0f }, a, a), a), "degenerate segment returns its point");

        float t = -1.0f;
        const float distance = SnapMath::DistanceToSegment2D({ 5.0f, 2.0f }, { 0.0f, 0.0f }, { 10.0f, 0.0f }, &t);
        Require(Near(distance, 2.0f) && Near(t, 0.5f), "2D segment distance and parameter");
    }

    void GridCases()
    {
        Require(Near(SnapMath::SnapToGrid(1.26f, 0.5f), 1.5f), "rounds up to nearest cell");
        Require(Near(SnapMath::SnapToGrid(-0.74f, 0.5f), -0.5f), "rounds negative values");
        Require(Near(SnapMath::SnapToGrid(3.3f, 0.0f), 3.3f), "zero cell size leaves value");

        // Axis drag along X of an off-grid object: Y and Z stay where they were.
        const Vector3 start{ 0.3f, 1.7f, -2.2f };
        const Vector3 dragged{ 2.8f, 1.7f, -2.2f };
        Require(Near(SnapMath::SnapMovedAxesToGrid(dragged, start, 1.0f), { 3.0f, 1.7f, -2.2f }), "only moved axes snap");
        Require(Near(SnapMath::SnapMovedAxesToGrid({ 2.8f, 0.4f, -2.2f }, start, 1.0f), { 3.0f, 0.0f, -2.2f }),
            "plane drag snaps both moved axes");
    }

    void RestAndRotationCases()
    {
        // Unit cube centred on its pivot resting on a floor: lowest point is 0.5 below the pivot.
        std::vector<Vector3> corners;
        for (int corner = 0; corner < 8; ++corner)
        {
            corners.push_back({ (corner & 1) ? 0.5f : -0.5f, (corner & 2) ? 0.5f : -0.5f, (corner & 4) ? 0.5f : -0.5f });
        }
        Require(Near(SnapMath::MinExtentAlong(corners, {}, Vector3::Up), -0.5f), "cube extends 0.5 below pivot");
        Require(Near(SnapMath::MinExtentAlong({}, {}, Vector3::Up), 0.0f), "no points gives no offset");

        const Quaternion toRight = SnapMath::RotationBetween(Vector3::Up, Vector3::Right);
        Require(Near(toRight.Rotate(Vector3::Up), Vector3::Right), "shortest arc maps up to right");
        const Quaternion flip = SnapMath::RotationBetween(Vector3::Up, -Vector3::Up);
        Require(Near(flip.Rotate(Vector3::Up), -Vector3::Up), "opposite vectors flip");
        const Quaternion same = SnapMath::RotationBetween(Vector3::Forward, Vector3::Forward);
        Require(Near(same.Rotate(Vector3::Right), Vector3::Right), "same vector is identity");
    }

    void SkinningCases()
    {
        const std::vector<Matrix4> skin = { Matrix4::Translation({ 1.0f, 0.0f, 0.0f }), Matrix4::Translation({ 0.0f, 2.0f, 0.0f }) };
        const uint16_t joints[4] = { 0, 1, 0, 0 };
        const float half[4] = { 0.5f, 0.5f, 0.0f, 0.0f };
        Require(Near(SnapMath::SkinPosition({ 0.0f, 0.0f, 0.0f }, joints, half, skin), { 0.5f, 1.0f, 0.0f }), "blends two joints");

        const float single[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        Require(Near(SnapMath::SkinPosition({ 1.0f, 1.0f, 1.0f }, joints, single, skin), { 2.0f, 1.0f, 1.0f }), "single joint");

        const uint16_t outOfRange[4] = { 7, 0, 0, 0 };
        const float weights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        Require(Near(SnapMath::SkinPosition({ 3.0f, 0.0f, 0.0f }, outOfRange, weights, skin), { 3.0f, 0.0f, 0.0f }),
            "missing joint keeps the bind position");
    }

    void ProjectionCases()
    {
        const Matrix4 view = Matrix4::LookTo({ 0.0f, 0.0f, -10.0f }, Vector3::Forward, Vector3::Up);
        const Matrix4 projection = Matrix4::Perspective(1.0471976f, 16.0f / 9.0f, 0.1f, 1000.0f);
        const Matrix4 viewProjection = view * projection;
        const SnapMath::ScreenRect rect{ 100.0f, 50.0f, 1600.0f, 900.0f };

        const auto centre = SnapMath::WorldToScreen(viewProjection, rect, { 0.0f, 0.0f, 0.0f });
        Require(centre && Near(centre->X, 900.0f, 0.01f) && Near(centre->Y, 500.0f, 0.01f), "origin projects to image centre");
        Require(!SnapMath::WorldToScreen(viewProjection, rect, { 0.0f, 0.0f, -20.0f }), "point behind camera is rejected");

        const Vector3 point{ 2.0f, 1.5f, 3.0f };
        const auto screen = SnapMath::WorldToScreen(viewProjection, rect, point);
        Require(screen.has_value(), "point in front projects");
        if (screen)
        {
            const SnapMath::Ray ray = SnapMath::ScreenRay(viewProjection, rect, screen->X, screen->Y);
            const Vector3 toPoint = (point - ray.origin).Normalized();
            Require(Near(Vector3::Dot(toPoint, ray.direction), 1.0f, 1e-4f), "screen ray passes back through projected point");
            Require(Near(ray.direction.Length(), 1.0f), "screen ray direction is normalized");
        }
    }
}

int main()
{
    RayTriangleCases();
    RayAabbCases();
    SegmentCases();
    GridCases();
    RestAndRotationCases();
    SkinningCases();
    ProjectionCases();

    if (g_Failures == 0)
    {
        std::printf("SnapMath validation passed\n");
        return 0;
    }
    std::printf("SnapMath validation: %d failure(s)\n", g_Failures);
    return 1;
}
