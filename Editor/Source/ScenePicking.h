#pragma once

#include <Engine.h>
#include "Scene/Component/SkinnedMeshRendererComponent.h"

#include <cstdint>
#include <vector>

namespace ScenePicking
{
    struct OrientedBox
    {
        Canape::Vector3 min;
        Canape::Vector3 max;
        Canape::Matrix4 toWorld;
    };

    std::vector<OrientedBox> SelectionBounds(Canape::GameObject& gameObject);
    // World bounds of a skinned mesh in its current pose (from per-bone bounds). Invalid when the bones are unresolved.
    Canape::MeshBounds PosedBounds(const Canape::SkinnedMeshRendererComponent& renderer, const Canape::MeshAsset& mesh);
    std::vector<uint64_t> SelectableComponentIds(Canape::GameObject& gameObject);
    Canape::GameObject* FindOwnerOfComponent(Canape::Scene& scene, uint64_t componentId);
}
