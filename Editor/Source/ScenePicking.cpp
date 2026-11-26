#include "ScenePicking.h"

#include "Scene/Component/LightComponent.h"
#include "Scene/Component/SkinnedMeshRendererComponent.h"
#include "Scene/Component/RenderableComponent.h"
#include "Simulation/TerrainGeneratorComponent.h"

#include <functional>

using namespace Canape;

namespace ScenePicking
{
    namespace
    {
        constexpr float kProxyRadius = 0.25f;

        void ForEachInSubtree(GameObject& root, const std::function<void(GameObject&)>& visit)
        {
            std::vector<GameObject*> stack{ &root };
            while (!stack.empty())
            {
                GameObject* current = stack.back();
                stack.pop_back();
                if (!current || current->IsPendingDestroy())
                {
                    continue;
                }
                visit(*current);
                for (TransformComponent* child : current->GetTransform()->GetChildren())
                {
                    if (child)
                    {
                        stack.push_back(child->GetOwnerObject());
                    }
                }
            }
        }

    }

    MeshBounds PosedBounds(const SkinnedMeshRendererComponent& renderer, const MeshAsset& mesh)
    {
        MeshBounds world;
        const std::vector<TransformComponent*>& bones = renderer.GetBones();
        const std::vector<MeshBounds>& local = mesh.GetBoneBounds();
        if (bones.size() != local.size())
        {
            return world;
        }
        for (size_t bone = 0; bone < bones.size(); ++bone)
        {
            if (!bones[bone] || !local[bone].IsValid())
            {
                continue;
            }
            const Matrix4 boneWorld = bones[bone]->GetLocalToWorldMatrix();
            for (int corner = 0; corner < 8; ++corner)
            {
                const Vector3 point{ (corner & 1) ? local[bone].Max.X : local[bone].Min.X,
                    (corner & 2) ? local[bone].Max.Y : local[bone].Min.Y,
                    (corner & 4) ? local[bone].Max.Z : local[bone].Min.Z };
                world.Add(boneWorld.TransformPoint(point));
            }
        }
        return world;
    }

    std::vector<OrientedBox> SelectionBounds(GameObject& gameObject)
    {
        AssetManager& assets = Application::Get().GetAssetManager();
        std::vector<OrientedBox> boxes;
        ForEachInSubtree(gameObject, [&](GameObject& current)
        {
            for (MeshRendererComponent* renderer : current.GetComponents<MeshRendererComponent>())
            {
                const MeshAsset* mesh = renderer ? assets.GetMesh(renderer->GetMesh()) : nullptr;
                if (!mesh)
                {
                    continue;
                }
                if (const auto* skinned = dynamic_cast<const SkinnedMeshRendererComponent*>(renderer))
                {
                    const MeshBounds posed = PosedBounds(*skinned, *mesh);
                    if (posed.IsValid())
                    {
                        boxes.push_back({ posed.Min, posed.Max, Matrix4::Identity });
                        continue;
                    }
                }
                if (mesh->GetBounds().IsValid())
                {
                    boxes.push_back({ mesh->GetBounds().Min, mesh->GetBounds().Max, renderer->GetTransform()->GetLocalToWorldMatrix() });
                }
            }
            for (auto* generator : current.GetComponents<TerrainGeneratorComponent>())
            {
                boxes.push_back({
                    TerrainBoxMin,
                    TerrainBoxMax,
                    generator->GetTransform()->GetLocalToWorldMatrix()
                });
            }
        });

        if (boxes.empty())
        {
            const Vector3 position = gameObject.GetTransform()->GetWorldPosition();
            const Vector3 radius{ kProxyRadius, kProxyRadius, kProxyRadius };
            boxes.push_back({ position - radius, position + radius, Matrix4::Identity });
        }
        return boxes;
    }

    std::vector<uint64_t> SelectableComponentIds(GameObject& gameObject)
    {
        std::vector<uint64_t> ids;
        ForEachInSubtree(gameObject, [&ids](GameObject& current)
        {
            for (Component* component : current.GetAllComponents())
            {
                if (dynamic_cast<RenderableComponent*>(component) || dynamic_cast<LightComponent*>(component)
                    || dynamic_cast<CameraComponent*>(component))
                {
                    ids.push_back(component->GetId());
                }
            }
        });
        return ids;
    }

    GameObject* FindOwnerOfComponent(Scene& scene, uint64_t componentId)
    {
        if (componentId == 0)
        {
            return nullptr;
        }
        for (GameObject* gameObject : scene.GetAllGameObjects())
        {
            if (!gameObject || gameObject->IsPendingDestroy())
            {
                continue;
            }
            for (Component* component : gameObject->GetAllComponents())
            {
                if (component->GetId() == componentId)
                {
                    return gameObject;
                }
            }
        }
        return nullptr;
    }
}
