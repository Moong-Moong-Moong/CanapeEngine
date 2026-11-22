#include "ScenePicking.h"

#include "Scene/Component/LightComponent.h"
#include "Scene/Component/SkinnedMeshRendererComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

using namespace Canape;

namespace ScenePicking
{
    namespace
    {
        constexpr float kProxyRadius = 0.25f; // cameras, lights and empty objects
        constexpr float kInfinity = std::numeric_limits<float>::infinity();

        struct Bounds
        {
            Vector3 min{ kInfinity, kInfinity, kInfinity };
            Vector3 max{ -kInfinity, -kInfinity, -kInfinity };

            void Add(const Vector3& p)
            {
                min = { std::min(min.X, p.X), std::min(min.Y, p.Y), std::min(min.Z, p.Z) };
                max = { std::max(max.X, p.X), std::max(max.Y, p.Y), std::max(max.Z, p.Z) };
            }
            bool Valid() const { return min.X <= max.X; }
        };

        // Local bounds of a static mesh never change, so they are kept per mesh asset.
        std::unordered_map<const MeshAsset*, Bounds> g_StaticBounds;

        const Bounds& StaticBounds(const MeshAsset& mesh)
        {
            auto found = g_StaticBounds.find(&mesh);
            if (found != g_StaticBounds.end() && found->second.Valid())
            {
                return found->second;
            }
            Bounds bounds;
            for (const StaticVertex& v : mesh.GetVertices())
            {
                bounds.Add(v.Position);
            }
            return g_StaticBounds[&mesh] = bounds;
        }

        Vector3 TransformPoint(const Vector3& p, const Matrix4& m) { return m.TransformPoint(p); }

        Vector3 TransformDirection(const Vector3& d, const Matrix4& m)
        {
            return Vector3::FromXM(DirectX::XMVector3TransformNormal(d.ToXM(), m.ToXM()));
        }

        bool ActiveInHierarchy(const GameObject& gameObject)
        {
            for (const TransformComponent* t = gameObject.GetTransform(); t; t = t->GetParent())
            {
                const GameObject* owner = t->GetOwnerObject();
                if (owner && !owner->IsActive())
                {
                    return false;
                }
            }
            return true;
        }

        // Slab test; returns the entry distance along the (unnormalized) ray, or infinity.
        float RayBox(const Vector3& origin, const Vector3& direction, const Bounds& box)
        {
            float tMin = 0.0f, tMax = kInfinity;
            const float o[3] = { origin.X, origin.Y, origin.Z }, d[3] = { direction.X, direction.Y, direction.Z };
            const float lo[3] = { box.min.X, box.min.Y, box.min.Z }, hi[3] = { box.max.X, box.max.Y, box.max.Z };
            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::abs(d[axis]) < 1e-12f)
                {
                    if (o[axis] < lo[axis] || o[axis] > hi[axis]) return kInfinity;
                    continue;
                }
                float t0 = (lo[axis] - o[axis]) / d[axis], t1 = (hi[axis] - o[axis]) / d[axis];
                if (t0 > t1) std::swap(t0, t1);
                tMin = std::max(tMin, t0);
                tMax = std::min(tMax, t1);
                if (tMin > tMax) return kInfinity;
            }
            return tMin;
        }

        // Möller-Trumbore, both faces; distance along the (unnormalized) ray, or infinity.
        float RayTriangle(const Vector3& origin, const Vector3& direction, const Vector3& a, const Vector3& b, const Vector3& c)
        {
            const Vector3 e1 = b - a, e2 = c - a;
            const Vector3 p = Vector3::Cross(direction, e2);
            const float det = Vector3::Dot(e1, p);
            if (std::abs(det) < 1e-12f) return kInfinity;
            const float inv = 1.0f / det;
            const Vector3 s = origin - a;
            const float u = Vector3::Dot(s, p) * inv;
            if (u < 0.0f || u > 1.0f) return kInfinity;
            const Vector3 q = Vector3::Cross(s, e1);
            const float v = Vector3::Dot(direction, q) * inv;
            if (v < 0.0f || u + v > 1.0f) return kInfinity;
            const float t = Vector3::Dot(e2, q) * inv;
            return t > 0.0f ? t : kInfinity;
        }

        // Nearest hit of a triangle list given positions (in the same frame as origin/direction).
        template<typename PositionAt>
        float RayMesh(const Vector3& origin, const Vector3& direction, const MeshAsset& mesh, PositionAt positionAt)
        {
            const std::vector<uint32_t>& indices = mesh.GetIndices();
            float best = kInfinity;
            for (const SubMesh& sub : mesh.GetSubMeshes())
            {
                const uint32_t end = std::min<uint32_t>(sub.startIndex + sub.indexCount, static_cast<uint32_t>(indices.size()));
                for (uint32_t i = sub.startIndex; i + 2 < end; i += 3)
                {
                    const float t = RayTriangle(origin, direction,
                        positionAt(sub.baseVertex + indices[i]),
                        positionAt(sub.baseVertex + indices[i + 1]),
                        positionAt(sub.baseVertex + indices[i + 2]));
                    best = std::min(best, t);
                }
            }
            return best;
        }

        // World positions of a skinned mesh in its current pose (palette * renderer world = IBM * bone world).
        bool SkinnedWorldPositions(const SkinnedMeshRendererComponent& renderer, const MeshAsset& mesh, std::vector<Vector3>& out)
        {
            const std::vector<TransformComponent*>& bones = renderer.GetBones();
            const std::vector<Matrix4>& inverseBind = mesh.GetInverseBindMatrices();
            if (bones.size() != mesh.GetBoneCount() || inverseBind.size() != bones.size())
            {
                return false;
            }
            std::vector<Matrix4> palette(bones.size(), Matrix4::Identity);
            for (size_t j = 0; j < bones.size(); ++j)
            {
                if (bones[j]) palette[j] = inverseBind[j] * bones[j]->GetLocalToWorldMatrix();
            }
            const std::vector<SkinnedVertex>& vertices = mesh.GetSkinnedVertices();
            out.resize(vertices.size());
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                const SkinnedVertex& v = vertices[i];
                Vector3 p{};
                float total = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    if (v.Weights[k] <= 0.0f || v.Joints[k] >= palette.size()) continue;
                    p = p + palette[v.Joints[k]].TransformPoint(v.Position) * v.Weights[k];
                    total += v.Weights[k];
                }
                out[i] = total > 0.0f ? p * (1.0f / total) : renderer.GetTransform()->GetLocalToWorldMatrix().TransformPoint(v.Position);
            }
            return true;
        }

        // Per bone, the bind-pose box (in that bone's space) of the vertices it moves the most. Transforming these
        // few boxes by the current bone matrices bounds the posed mesh without skinning every vertex each frame.
        std::unordered_map<const MeshAsset*, std::vector<Bounds>> g_BoneBounds;

        const std::vector<Bounds>& BoneBounds(const MeshAsset& mesh)
        {
            auto found = g_BoneBounds.find(&mesh);
            if (found != g_BoneBounds.end())
            {
                return found->second;
            }
            const std::vector<Matrix4>& inverseBind = mesh.GetInverseBindMatrices();
            std::vector<Bounds> bounds(inverseBind.size());
            for (const SkinnedVertex& v : mesh.GetSkinnedVertices())
            {
                int strongest = -1;
                for (int k = 0; k < 4; ++k)
                {
                    if (v.Weights[k] > 0.0f && v.Joints[k] < inverseBind.size() && (strongest < 0 || v.Weights[k] > v.Weights[strongest]))
                    {
                        strongest = k;
                    }
                }
                if (strongest >= 0)
                {
                    const uint16_t joint = v.Joints[strongest];
                    bounds[joint].Add(inverseBind[joint].TransformPoint(v.Position));
                }
            }
            return g_BoneBounds[&mesh] = std::move(bounds);
        }

        // World box of a skinned mesh in its current pose, from the per-bone boxes.
        Bounds SkinnedWorldBounds(const SkinnedMeshRendererComponent& renderer, const MeshAsset& mesh)
        {
            Bounds world;
            const std::vector<TransformComponent*>& bones = renderer.GetBones();
            const std::vector<Bounds>& local = BoneBounds(mesh);
            if (bones.size() != local.size())
            {
                return world;
            }
            for (size_t j = 0; j < bones.size(); ++j)
            {
                if (!bones[j] || !local[j].Valid()) continue;
                const Matrix4 boneWorld = bones[j]->GetLocalToWorldMatrix();
                for (int i = 0; i < 8; ++i)
                {
                    const Vector3 corner{ (i & 1) ? local[j].max.X : local[j].min.X,
                        (i & 2) ? local[j].max.Y : local[j].min.Y, (i & 4) ? local[j].max.Z : local[j].min.Z };
                    world.Add(boneWorld.TransformPoint(corner));
                }
            }
            return world;
        }

        float RaySphere(const Ray& ray, const Vector3& center, float radius)
        {
            const Vector3 oc = ray.origin - center;
            const float b = Vector3::Dot(oc, ray.direction);
            const float c = Vector3::Dot(oc, oc) - radius * radius;
            const float h = b * b - c;
            if (h < 0.0f) return kInfinity;
            const float t = -b - std::sqrt(h);
            return t > 0.0f ? t : (c < 0.0f ? 0.0f : kInfinity);
        }
    }

    Ray RayFromView(const Matrix4& view, const Matrix4& projection, float ndcX, float ndcY)
    {
        const Matrix4 inverse = (view * projection).Inverse();
        const auto unproject = [&inverse](float x, float y, float z)
        {
            const DirectX::XMVECTOR p = DirectX::XMVector4Transform(DirectX::XMVectorSet(x, y, z, 1.0f), inverse.ToXM());
            const float w = DirectX::XMVectorGetW(p);
            return Vector3(DirectX::XMVectorGetX(p) / w, DirectX::XMVectorGetY(p) / w, DirectX::XMVectorGetZ(p) / w);
        };
        const Vector3 nearPoint = unproject(ndcX, ndcY, 0.0f);
        const Vector3 farPoint = unproject(ndcX, ndcY, 1.0f);
        return { nearPoint, (farPoint - nearPoint).Normalized() };
    }

    GameObject* Pick(Scene& scene, const Ray& ray)
    {
        AssetManager& assets = Application::Get().GetAssetManager();
        GameObject* best = nullptr;
        float bestDistance = kInfinity;
        std::vector<Vector3> skinned;

        for (GameObject* gameObject : scene.GetAllGameObjects())
        {
            if (!gameObject || !ActiveInHierarchy(*gameObject))
            {
                continue;
            }

            float distance = kInfinity;
            bool hasMesh = false;
            for (MeshRendererComponent* renderer : gameObject->GetComponents<MeshRendererComponent>())
            {
                if (!renderer || !renderer->IsEnabled()) continue;
                const MeshAsset* mesh = assets.GetMesh(renderer->GetMesh());
                if (!mesh) continue;
                hasMesh = true;

                if (const auto* skin = dynamic_cast<const SkinnedMeshRendererComponent*>(renderer))
                {
                    // Cheap posed box first; only skin every vertex when the ray can hit this mesh.
                    if (RayBox(ray.origin, ray.direction, SkinnedWorldBounds(*skin, *mesh)) >= bestDistance) continue;
                    if (!SkinnedWorldPositions(*skin, *mesh, skinned)) continue;
                    distance = std::min(distance, RayMesh(ray.origin, ray.direction, *mesh,
                        [&skinned](size_t i) { return i < skinned.size() ? skinned[i] : Vector3{}; }));
                }
                else
                {
                    // Test in mesh space; t along the transformed (unnormalized) direction is the same point,
                    // so the world distance comes back through the hit point.
                    const Matrix4 toWorld = renderer->GetTransform()->GetLocalToWorldMatrix();
                    const Matrix4 toLocal = toWorld.Inverse();
                    const Vector3 origin = TransformPoint(ray.origin, toLocal);
                    const Vector3 direction = TransformDirection(ray.direction, toLocal);
                    if (RayBox(origin, direction, StaticBounds(*mesh)) == kInfinity) continue;
                    const std::vector<StaticVertex>& vertices = mesh->GetVertices();
                    const float t = RayMesh(origin, direction, *mesh,
                        [&vertices](size_t i) { return i < vertices.size() ? vertices[i].Position : Vector3{}; });
                    if (t < kInfinity)
                    {
                        const Vector3 hit = TransformPoint(origin + direction * t, toWorld);
                        distance = std::min(distance, (hit - ray.origin).Length());
                    }
                }
            }

            // Cameras and lights have no mesh: a small sphere stands in for them.
            if (!hasMesh && (gameObject->GetComponent<CameraComponent>() || gameObject->GetComponent<LightComponent>()))
            {
                distance = RaySphere(ray, gameObject->GetTransform()->GetWorldPosition(), kProxyRadius);
            }

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = gameObject;
            }
        }
        return best;
    }

    std::vector<OrientedBox> SelectionBounds(GameObject& gameObject)
    {
        AssetManager& assets = Application::Get().GetAssetManager();
        std::vector<OrientedBox> boxes;

        std::vector<GameObject*> stack{ &gameObject };
        while (!stack.empty())
        {
            GameObject* current = stack.back();
            stack.pop_back();
            if (!current) continue;

            for (MeshRendererComponent* renderer : current->GetComponents<MeshRendererComponent>())
            {
                if (!renderer) continue;
                const MeshAsset* mesh = assets.GetMesh(renderer->GetMesh());
                if (!mesh) continue;
                if (const auto* skin = dynamic_cast<const SkinnedMeshRendererComponent*>(renderer))
                {
                    const Bounds box = SkinnedWorldBounds(*skin, *mesh);
                    if (box.Valid()) boxes.push_back({ box.min, box.max, Matrix4::Identity });
                }
                else
                {
                    const Bounds& box = StaticBounds(*mesh);
                    if (box.Valid()) boxes.push_back({ box.min, box.max, renderer->GetTransform()->GetLocalToWorldMatrix() });
                }
            }

            for (TransformComponent* child : current->GetTransform()->GetChildren())
            {
                if (child) stack.push_back(child->GetOwnerObject());
            }
        }

        if (boxes.empty())
        {
            const Vector3 p = gameObject.GetTransform()->GetWorldPosition();
            const Vector3 r{ kProxyRadius, kProxyRadius, kProxyRadius };
            boxes.push_back({ p - r, p + r, Matrix4::Identity });
        }
        return boxes;
    }
}
