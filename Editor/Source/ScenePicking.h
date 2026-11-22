#pragma once

#include <Engine.h>

#include <vector>

// Mouse picking and selection bounds for the editor scene view, done on the CPU against the meshes in the scene.
namespace ScenePicking
{
    struct Ray
    {
        Canape::Vector3 origin;
        Canape::Vector3 direction; // normalized
    };

    // A box in a local frame: corners are min/max transformed by toWorld.
    struct OrientedBox
    {
        Canape::Vector3 min;
        Canape::Vector3 max;
        Canape::Matrix4 toWorld;
    };

    // ndc: x, y in [-1, 1], +y up. view and projection use the engine's row-vector convention.
    Ray RayFromView(const Canape::Matrix4& view, const Canape::Matrix4& projection, float ndcX, float ndcY);

    // The nearest object whose mesh the ray hits (cameras and lights count as small spheres), or nullptr.
    Canape::GameObject* Pick(Canape::Scene& scene, const Ray& ray);

    // Boxes around everything visible of gameObject and its children, for drawing the selection.
    // Objects with nothing to draw get a small box at their position.
    std::vector<OrientedBox> SelectionBounds(Canape::GameObject& gameObject);
}
