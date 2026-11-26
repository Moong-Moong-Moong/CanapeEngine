#pragma once

#include "Snapping/SnapMath.h"

#include <Engine.h>

#include <optional>
#include <unordered_map>
#include <vector>

namespace Canape
{
    class SkinnedMeshRendererComponent;
}

enum class SnapMode
{
    Off,
    Vertex,
    Edge,
    Face
};

const char* GetSnapModeName(SnapMode mode);

struct SnapTarget
{
    SnapMode mode = SnapMode::Off;
    Canape::Vector3 position;
    Canape::Vector3 normal = Canape::Vector3::Up; // surface normal, facing the camera
    bool hasEdge = false;
    Canape::Vector3 edgeA;
    Canape::Vector3 edgeB;
};

// CPU queries against the triangles of every mesh / skinned mesh renderer in a scene.
// Skinned meshes are posed on the CPU from their current bones, cached for the current ImGui frame.
class SceneSnapper
{
public:
    struct Query
    {
        SnapMath::Ray ray;
        Canape::Matrix4 viewProjection;
        SnapMath::ScreenRect rect;
        Canape::Vector2 mouse;
        // These objects and their children are never snap targets (the objects being moved).
        std::vector<const Canape::GameObject*> exclude;
    };

    std::optional<SnapTarget> FindTarget(Canape::Scene& scene, SnapMode mode, const Query& query);

    // Vertex of `root` (or its children) closest to the mouse on screen, within `radiusPixels`.
    std::optional<Canape::Vector3> FindNearestVertex(Canape::GameObject& root, const Query& query, float radiusPixels);

private:
    struct WorldMesh
    {
        const Canape::MeshAsset* mesh = nullptr;
        Canape::Matrix4 world;            // static meshes: local -> world
        const std::vector<Canape::Vector3>* posed = nullptr; // skinned meshes: world-space positions
        Canape::Vector3 boundsMin;
        Canape::Vector3 boundsMax;
    };

    struct SurfaceHit
    {
        float distance = 0.0f;
        Canape::Vector3 position;
        Canape::Vector3 normal;
        Canape::Vector3 triangle[3];
        size_t meshIndex = 0;
    };

    void BeginFrame();
    void CollectMeshes(Canape::GameObject& gameObject, std::vector<WorldMesh>& out);
    std::vector<WorldMesh> CollectScene(Canape::Scene& scene, const std::vector<const Canape::GameObject*>& exclude);
    std::vector<WorldMesh> CollectSubtree(Canape::GameObject& root);
    std::optional<SurfaceHit> Raycast(const std::vector<WorldMesh>& meshes, const SnapMath::Ray& ray) const;
    const std::vector<Canape::Vector3>& PosedVertices(const Canape::SkinnedMeshRendererComponent& renderer,
        const Canape::MeshAsset& mesh);

    std::optional<SnapTarget> VertexTarget(const std::vector<WorldMesh>& meshes, const std::optional<SurfaceHit>& hit,
        const Query& query) const;
    std::optional<SnapTarget> EdgeTarget(const SurfaceHit& hit, const Query& query) const;

    static Canape::Vector3 VertexPosition(const WorldMesh& mesh, size_t index);
    static size_t VertexCount(const WorldMesh& mesh);

    int m_CacheFrame = -1;
    std::unordered_map<const void*, std::vector<Canape::Vector3>> m_PosedCache;
};
