#pragma once

#include "EditorContext.h"

#include <cstdint>

class HierarchyPanel
{
public:
    static constexpr const char* kName = "Hierarchy";

    void Draw(EditorContext& context, Canape::Scene* scene);

private:
    struct Reparent
    {
        uint64_t childId = 0;
        uint64_t parentId = 0;
        bool pending = false;
    };

    void DrawNode(EditorContext& context, Canape::GameObject& gameObject);
    void DrawBackground(EditorContext& context);
    void ApplyPendingActions(EditorContext& context, Canape::Scene& scene);

    Reparent m_Reparent;
    uint64_t m_PressedId = 0;
    uint64_t m_DeleteId = 0;
    uint64_t m_CreateParentId = 0;
    bool m_CreateRequested = false;
};
