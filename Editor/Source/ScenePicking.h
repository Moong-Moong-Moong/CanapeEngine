#pragma once

#include <Engine.h>

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
    std::vector<uint64_t> SelectableComponentIds(Canape::GameObject& gameObject);
    Canape::GameObject* FindOwnerOfComponent(Canape::Scene& scene, uint64_t componentId);
}
