#include "Snapping/SceneSnapper.h"
#include "ScenePicking.h"

#include "Scene/Component/SkinnedMeshRendererComponent.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>

using namespace Canape;
using SnapMath::Ray;

namespace
{
    // Screen radius in which vertices / edge midpoints pull the target.
    constexpr float kVertexRadius = 14.0f;
    constexpr float kMidpointRadius = 10.0f;

    bool IsActiveInHierarchy(const GameObject& gameObject)
    {
        if (gameObject.IsPendingDestroy())
        {
            return false;
        }
        for (const TransformComponent* transform = gameObject.GetTransform(); transform; transform = transform->GetParent())
        {
            const GameObject* owner = transform->GetOwnerObject();
            if (owner && !owner->IsActive())
            {
                return false;
            }
        }
        return true;
    }

    bool IsInSubtree(const GameObject& gameObject, const GameObject* root)
    {
        if (!root)
        {
            return false;
        }
        for (const TransformComponent* transform = gameObject.GetTransform(); transform; transform = transform->GetParent())
        {
            if (transform->GetOwnerObject() == root)
            {
                return true;
            }
        }
        return false;
    }

    float ScreenDistance(const Vector2& a, const Vector2& b)
    {
        return (a - b).Length();
    }
}

const char* GetSnapModeName(SnapMode mode)
{
    switch (mode)
    {
    case SnapMode::Vertex: return "Vertex";
    case SnapMode::Edge: return "Edge";
    case SnapMode::Face: return "Face";
    default: return "Off";
    }
}

void SceneSnapper::BeginFrame()
{
    const int frame = ImGui::GetFrameCount();
    if (frame != m_CacheFrame)
    {
        m_CacheFrame = frame;
        m_PosedCache.clear();
    }
}

const std::vector<Vector3>& SceneSnapper::PosedVertices(const SkinnedMeshRendererComponent& renderer, const MeshAsset& mesh)
{
    auto [entry, inserted] = m_PosedCache.try_emplace(&renderer);
    std::vector<Vector3>& posed = entry->second;
    if (!inserted)
    {
        return posed;
    }

    // Same palette the renderer builds, but kept in world space: inverseBind * boneWorld.
    const Matrix4 rendererWorld = renderer.GetTransform()->GetLocalToWorldMatrix();
    const std::vector<TransformComponent*>& bones = renderer.GetBones();
    const std::vector<Matrix4>& inverseBind = mesh.GetInverseBindMatrices();
    const std::vector<SkinnedVertex>& vertices = mesh.GetSkinnedVertices();
    posed.resize(vertices.size());

    if (bones.size() != inverseBind.size() || bones.empty())
    {
        // Unresolved skeleton: fall back to the bind pose under the renderer transform.
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            posed[i] = rendererWorld.TransformPoint(vertices[i].Position);
        }
        return posed;
    }

    std::vector<Matrix4> skin(bones.size());
    for (size_t joint = 0; joint < bones.size(); ++joint)
    {
        skin[joint] = bones[joint] ? inverseBind[joint] * bones[joint]->GetLocalToWorldMatrix() : rendererWorld;
    }
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        posed[i] = SnapMath::SkinPosition(vertices[i].Position, vertices[i].Joints, vertices[i].Weights, skin);
    }
    return posed;
}

void SceneSnapper::CollectMeshes(GameObject& gameObject, std::vector<WorldMesh>& out)
{
    AssetManager& assets = Application::Get().GetAssetManager();
    for (MeshRendererComponent* renderer : gameObject.GetComponents<MeshRendererComponent>())
    {
        const MeshAsset* mesh = renderer ? assets.GetMesh(renderer->GetMesh()) : nullptr;
        if (!mesh || mesh->GetIndices().empty())
        {
            continue;
        }

        WorldMesh entry;
        entry.mesh = mesh;
        MeshBounds bounds;
        if (const auto* skinned = dynamic_cast<const SkinnedMeshRendererComponent*>(renderer);
            skinned && mesh->GetVertexFormat() == VertexFormat::Skinned)
        {
            bounds = ScenePicking::PosedBounds(*skinned, *mesh);
            entry.posed = &PosedVertices(*skinned, *mesh);
            if (!bounds.IsValid())
            {
                for (const Vector3& position : *entry.posed)
                {
                    bounds.Add(position);
                }
            }
        }
        else
        {
            entry.world = renderer->GetTransform()->GetLocalToWorldMatrix();
            const MeshBounds& local = mesh->GetBounds();
            if (!local.IsValid())
            {
                continue;
            }
            for (int corner = 0; corner < 8; ++corner)
            {
                bounds.Add(entry.world.TransformPoint({ (corner & 1) ? local.Max.X : local.Min.X,
                    (corner & 2) ? local.Max.Y : local.Min.Y, (corner & 4) ? local.Max.Z : local.Min.Z }));
            }
        }
        if (!bounds.IsValid())
        {
            continue;
        }
        entry.boundsMin = bounds.Min;
        entry.boundsMax = bounds.Max;
        out.push_back(entry);
    }
}

std::vector<SceneSnapper::WorldMesh> SceneSnapper::CollectScene(Scene& scene, const std::vector<const GameObject*>& exclude)
{
    std::vector<WorldMesh> meshes;
    for (GameObject* gameObject : scene.GetAllGameObjects())
    {
        if (gameObject && IsActiveInHierarchy(*gameObject)
            && std::none_of(exclude.begin(), exclude.end(), [&](const GameObject* root) { return IsInSubtree(*gameObject, root); }))
        {
            CollectMeshes(*gameObject, meshes);
        }
    }
    return meshes;
}

std::vector<SceneSnapper::WorldMesh> SceneSnapper::CollectSubtree(GameObject& root)
{
    std::vector<WorldMesh> meshes;
    std::vector<GameObject*> stack{ &root };
    while (!stack.empty())
    {
        GameObject* current = stack.back();
        stack.pop_back();
        if (!current || current->IsPendingDestroy())
        {
            continue;
        }
        CollectMeshes(*current, meshes);
        for (TransformComponent* child : current->GetTransform()->GetChildren())
        {
            stack.push_back(child->GetOwnerObject());
        }
    }
    return meshes;
}

size_t SceneSnapper::VertexCount(const WorldMesh& mesh)
{
    return mesh.posed ? mesh.posed->size() : mesh.mesh->GetVertices().size();
}

Vector3 SceneSnapper::VertexPosition(const WorldMesh& mesh, size_t index)
{
    return mesh.posed ? (*mesh.posed)[index] : mesh.world.TransformPoint(mesh.mesh->GetVertices()[index].Position);
}

std::optional<SceneSnapper::SurfaceHit> SceneSnapper::Raycast(const std::vector<WorldMesh>& meshes, const Ray& ray) const
{
    std::optional<SurfaceHit> best;
    for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
    {
        const WorldMesh& mesh = meshes[meshIndex];
        const std::optional<float> boxDistance = SnapMath::RayAabb(ray, mesh.boundsMin, mesh.boundsMax);
        if (!boxDistance || (best && *boxDistance > best->distance))
        {
            continue;
        }

        // Static meshes are tested in their own space: an affine map keeps the ray parameter, so t stays a world distance.
        Ray testRay = ray;
        if (!mesh.posed)
        {
            const Matrix4 toLocal = mesh.world.Inverse();
            testRay.origin = toLocal.TransformPoint(ray.origin);
            testRay.direction = toLocal.TransformPoint(ray.origin + ray.direction) - testRay.origin;
        }

        const std::vector<uint32_t>& indices = mesh.mesh->GetIndices();
        const size_t vertexCount = VertexCount(mesh);
        const auto position = [&](uint32_t index) -> Vector3
        {
            return mesh.posed ? (*mesh.posed)[index] : mesh.mesh->GetVertices()[index].Position;
        };

        for (const SubMesh& subMesh : mesh.mesh->GetSubMeshes())
        {
            const size_t end = std::min<size_t>(static_cast<size_t>(subMesh.startIndex) + subMesh.indexCount, indices.size());
            for (size_t i = subMesh.startIndex; i + 2 < end; i += 3)
            {
                const int64_t base = subMesh.baseVertex;
                const int64_t i0 = indices[i] + base;
                const int64_t i1 = indices[i + 1] + base;
                const int64_t i2 = indices[i + 2] + base;
                if (i0 < 0 || i1 < 0 || i2 < 0 || static_cast<size_t>(std::max({ i0, i1, i2 })) >= vertexCount)
                {
                    continue;
                }
                const Vector3 a = position(static_cast<uint32_t>(i0));
                const Vector3 b = position(static_cast<uint32_t>(i1));
                const Vector3 c = position(static_cast<uint32_t>(i2));
                const std::optional<float> t = SnapMath::RayTriangle(testRay, a, b, c);
                if (!t || (best && *t >= best->distance))
                {
                    continue;
                }
                SurfaceHit hit;
                hit.distance = *t;
                hit.meshIndex = meshIndex;
                if (mesh.posed)
                {
                    hit.triangle[0] = a;
                    hit.triangle[1] = b;
                    hit.triangle[2] = c;
                }
                else
                {
                    hit.triangle[0] = mesh.world.TransformPoint(a);
                    hit.triangle[1] = mesh.world.TransformPoint(b);
                    hit.triangle[2] = mesh.world.TransformPoint(c);
                }
                best = hit;
            }
        }
    }

    if (best)
    {
        best->position = ray.origin + ray.direction * best->distance;
        Vector3 normal = Vector3::Cross(best->triangle[1] - best->triangle[0], best->triangle[2] - best->triangle[0]).Normalized();
        if (Vector3::Dot(normal, ray.direction) > 0.0f)
        {
            normal = -normal;
        }
        best->normal = normal;
    }
    return best;
}

std::optional<SnapTarget> SceneSnapper::VertexTarget(const std::vector<WorldMesh>& meshes, const std::optional<SurfaceHit>& hit,
    const Query& query) const
{
    std::optional<SnapTarget> best;
    float bestDistance = FLT_MAX;
    const auto consider = [&](const Vector3& world, float limit)
    {
        const std::optional<Vector2> screen = SnapMath::WorldToScreen(query.viewProjection, query.rect, world);
        if (!screen)
        {
            return;
        }
        const float distance = ScreenDistance(*screen, query.mouse);
        if (distance < limit && distance < bestDistance)
        {
            bestDistance = distance;
            best = SnapTarget{ SnapMode::Vertex, world, hit ? hit->normal : Vector3::Up };
        }
    };

    // The corners of the triangle under the cursor always qualify, however far they are on screen.
    if (hit)
    {
        for (const Vector3& corner : hit->triangle)
        {
            consider(corner, FLT_MAX);
        }
    }

    // Nearby vertices of meshes the ray passes, unless they sit behind the surface under the cursor.
    for (const WorldMesh& mesh : meshes)
    {
        const Vector3 margin = (mesh.boundsMax - mesh.boundsMin) * 0.05f + Vector3{ 0.01f, 0.01f, 0.01f };
        if (!SnapMath::RayAabb(query.ray,mesh.boundsMin - margin, mesh.boundsMax + margin))
        {
            continue;
        }
        const size_t count = VertexCount(mesh);
        for (size_t i = 0; i < count; ++i)
        {
            const Vector3 world = VertexPosition(mesh, i);
            if (hit)
            {
                const float depth = Vector3::Dot(world - query.ray.origin, query.ray.direction);
                if (depth > hit->distance * 1.01f + 0.01f)
                {
                    continue;
                }
            }
            consider(world, kVertexRadius);
        }
    }
    return best;
}

std::optional<SnapTarget> SceneSnapper::EdgeTarget(const SurfaceHit& hit, const Query& query) const
{
    int bestEdge = -1;
    float bestDistance = FLT_MAX;
    std::optional<Vector2> screen[3];
    for (int corner = 0; corner < 3; ++corner)
    {
        screen[corner] = SnapMath::WorldToScreen(query.viewProjection, query.rect, hit.triangle[corner]);
    }
    for (int edge = 0; edge < 3; ++edge)
    {
        const int a = edge;
        const int b = (edge + 1) % 3;
        if (!screen[a] || !screen[b])
        {
            continue;
        }
        const float distance = SnapMath::DistanceToSegment2D(query.mouse, *screen[a], *screen[b]);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestEdge = edge;
        }
    }
    if (bestEdge < 0)
    {
        return std::nullopt;
    }

    SnapTarget target;
    target.mode = SnapMode::Edge;
    target.normal = hit.normal;
    target.hasEdge = true;
    target.edgeA = hit.triangle[bestEdge];
    target.edgeB = hit.triangle[(bestEdge + 1) % 3];
    target.position = SnapMath::ClosestPointOnSegment(hit.position, target.edgeA, target.edgeB);

    const Vector3 midpoint = (target.edgeA + target.edgeB) * 0.5f;
    if (const std::optional<Vector2> midScreen = SnapMath::WorldToScreen(query.viewProjection, query.rect, midpoint);
        midScreen && ScreenDistance(*midScreen, query.mouse) < kMidpointRadius)
    {
        target.position = midpoint;
    }
    return target;
}

std::optional<SnapTarget> SceneSnapper::FindTarget(Scene& scene, SnapMode mode, const Query& query)
{
    if (mode == SnapMode::Off)
    {
        return std::nullopt;
    }
    BeginFrame();
    const std::vector<WorldMesh> meshes = CollectScene(scene, query.exclude);
    const std::optional<SurfaceHit> hit = Raycast(meshes, query.ray);

    switch (mode)
    {
    case SnapMode::Vertex:
        return VertexTarget(meshes, hit, query);
    case SnapMode::Edge:
        return hit ? EdgeTarget(*hit, query) : std::nullopt;
    case SnapMode::Face:
        if (hit)
        {
            return SnapTarget{ SnapMode::Face, hit->position, hit->normal };
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

std::optional<Vector3> SceneSnapper::FindNearestVertex(GameObject& root, const Query& query, float radiusPixels)
{
    BeginFrame();
    std::optional<Vector3> best;
    float bestDistance = radiusPixels;
    for (const WorldMesh& mesh : CollectSubtree(root))
    {
        const size_t count = VertexCount(mesh);
        for (size_t i = 0; i < count; ++i)
        {
            const Vector3 world = VertexPosition(mesh, i);
            const std::optional<Vector2> screen = SnapMath::WorldToScreen(query.viewProjection, query.rect, world);
            if (!screen)
            {
                continue;
            }
            const float distance = ScreenDistance(*screen, query.mouse);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = world;
            }
        }
    }
    return best;
}
