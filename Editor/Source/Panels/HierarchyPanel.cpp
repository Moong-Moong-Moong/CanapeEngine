#include "Panels/HierarchyPanel.h"
#include "EditorUtils.h"

#include <imgui.h>

#include <algorithm>
#include <vector>

using namespace Canape;

namespace
{
    void CollectSubtree(TransformComponent& transform, std::vector<GameObject*>& result)
    {
        for (TransformComponent* child : transform.GetChildren())
        {
            CollectSubtree(*child, result);
        }
        result.push_back(transform.GetOwnerObject());
    }
}

void HierarchyPanel::Draw(EditorContext& context, Scene* scene)
{
    if (!ImGui::Begin(kName))
    {
        ImGui::End();
        return;
    }

    if (!scene)
    {
        ImGui::TextDisabled("No active scene");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("%s", scene->GetName().c_str());
    ImGui::Separator();

    for (GameObject* gameObject : scene->GetAllGameObjects())
    {
        if (!gameObject->IsPendingDestroy() && !gameObject->GetTransform()->GetParent())
        {
            DrawNode(context, *gameObject);
        }
    }

    DrawBackground(context);

    if (m_PressedId != 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        context.Select(scene->GetGameObject(scene->FindGameObjectById(m_PressedId)));
        m_PressedId = 0;
    }

    ApplyPendingActions(context, *scene);
    ImGui::End();
}

void HierarchyPanel::DrawNode(EditorContext& context, GameObject& gameObject)
{
    const TransformComponent* transform = gameObject.GetTransform();
    const bool selected = context.GetSelected(gameObject.GetScene()) == &gameObject;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (selected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (transform->GetChildCount() == 0)
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    if (!gameObject.IsActive())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(gameObject.GetId())),
        flags, "%s", gameObject.GetName().c_str());
    if (!gameObject.IsActive())
    {
        ImGui::PopStyleColor();
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        m_PressedId = gameObject.GetId();
    }

    if (ImGui::BeginDragDropSource())
    {
        m_PressedId = 0;
        const uint64_t id = gameObject.GetId();
        ImGui::SetDragDropPayload(EditorUtils::kGameObjectPayload, &id, sizeof(id));
        ImGui::TextUnformatted(gameObject.GetName().c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorUtils::kGameObjectPayload))
        {
            m_Reparent = { *static_cast<const uint64_t*>(payload->Data), gameObject.GetId(), true };
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Create Child"))
        {
            m_CreateRequested = true;
            m_CreateParentId = gameObject.GetId();
        }
        if (ImGui::MenuItem("Delete"))
        {
            m_DeleteId = gameObject.GetId();
        }
        ImGui::EndPopup();
    }

    if (open)
    {
        for (TransformComponent* child : transform->GetChildren())
        {
            GameObject* childObject = child->GetOwnerObject();
            if (childObject && !childObject->IsPendingDestroy())
            {
                DrawNode(context, *childObject);
            }
        }
        ImGui::TreePop();
    }
}

void HierarchyPanel::DrawBackground(EditorContext& context)
{
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##HierarchyBackground", ImVec2(std::max(available.x, 1.0f), std::max(available.y, 1.0f)));

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        context.ClearSelection();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorUtils::kGameObjectPayload))
        {
            m_Reparent = { *static_cast<const uint64_t*>(payload->Data), 0, true };
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem("##HierarchyBackgroundMenu"))
    {
        if (ImGui::MenuItem("Create Empty"))
        {
            m_CreateRequested = true;
            m_CreateParentId = 0;
        }
        ImGui::EndPopup();
    }
}

void HierarchyPanel::ApplyPendingActions(EditorContext& context, Scene& scene)
{
    if (m_Reparent.pending)
    {
        GameObject* child = scene.GetGameObject(scene.FindGameObjectById(m_Reparent.childId));
        GameObject* parent = m_Reparent.parentId ? scene.GetGameObject(scene.FindGameObjectById(m_Reparent.parentId)) : nullptr;
        if (child && (parent || m_Reparent.parentId == 0))
        {
            EditorUtils::SetParentKeepWorld(child->GetTransform(), parent ? parent->GetTransform() : nullptr);
        }
        m_Reparent = {};
    }

    if (m_CreateRequested)
    {
        GameObject* created = scene.GetGameObject(scene.AddGameObject("GameObject"));
        GameObject* parent = m_CreateParentId ? scene.GetGameObject(scene.FindGameObjectById(m_CreateParentId)) : nullptr;
        if (created && parent)
        {
            created->GetTransform()->SetParent(parent->GetTransform());
        }
        context.Select(created);
        m_CreateRequested = false;
        m_CreateParentId = 0;
    }

    if (m_DeleteId != 0)
    {
        if (GameObject* target = scene.GetGameObject(scene.FindGameObjectById(m_DeleteId)))
        {
            std::vector<GameObject*> subtree;
            CollectSubtree(*target->GetTransform(), subtree);
            for (GameObject* gameObject : subtree)
            {
                scene.RemoveGameObject(gameObject->GetHandle());
            }
        }
        m_DeleteId = 0;
    }
}
