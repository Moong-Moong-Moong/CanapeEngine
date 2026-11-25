#include "Panels/HierarchyPanel.h"
#include "EditorUtils.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <filesystem>
#include <string>
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

    std::string ToLower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
        return text;
    }

    std::string ParentPath(const GameObject& gameObject)
    {
        std::string path;
        for (const TransformComponent* parent = gameObject.GetTransform()->GetParent(); parent; parent = parent->GetParent())
        {
            const GameObject* owner = parent->GetOwnerObject();
            path = (owner ? owner->GetName() : std::string("?")) + (path.empty() ? "" : "/" + path);
        }
        return path;
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

    const std::string& scenePath = context.GetScenePath();
    const std::string sceneKey = scenePath.empty() ? "untitled:" + scene->GetName() : scenePath;
    if (sceneKey != m_SceneKey)
    {
        m_SceneKey = sceneKey;
        ImGui::GetStateStorage()->Clear();
        m_RenamingId = 0;
    }

    const std::string sceneName = scenePath.empty()
        ? scene->GetName()
        : EditorUtils::ToUtf8(std::filesystem::path(scenePath).stem());
    ImGui::TextUnformatted(sceneName.c_str());

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##Search", "Search", &m_Search);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && m_RenamingId == 0 && !ImGui::GetIO().WantTextInput
        && ImGui::IsKeyPressed(ImGuiKey_F2, false))
    {
        if (const GameObject* selected = context.GetSelected(scene))
        {
            BeginRename(*selected);
        }
    }

    ImGui::Separator();

    if (m_Search.empty())
    {
        for (GameObject* gameObject : scene->GetAllGameObjects())
        {
            if (!gameObject->IsPendingDestroy() && !gameObject->GetTransform()->GetParent())
            {
                DrawNode(context, *gameObject);
            }
        }
    }
    else
    {
        DrawSearchResults(context, *scene);
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
    const bool renaming = m_RenamingId == gameObject.GetId();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
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
        flags, "%s", renaming ? "" : gameObject.GetName().c_str());
    if (!gameObject.IsActive())
    {
        ImGui::PopStyleColor();
    }

    DrawItemInteractions(context, gameObject);
    if (renaming)
    {
        DrawRename(gameObject);
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

void HierarchyPanel::DrawSearchResults(EditorContext& context, Scene& scene)
{
    const std::string query = ToLower(m_Search);
    size_t matches = 0;
    for (GameObject* gameObject : scene.GetAllGameObjects())
    {
        if (gameObject->IsPendingDestroy() || ToLower(gameObject->GetName()).find(query) == std::string::npos)
        {
            continue;
        }
        ++matches;

        ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(gameObject->GetId())));
        const bool selected = context.GetSelected(&scene) == gameObject;
        const bool renaming = m_RenamingId == gameObject->GetId();

        if (!gameObject->IsActive())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        ImGui::Selectable(renaming ? "##Row" : gameObject->GetName().c_str(), selected, ImGuiSelectableFlags_AllowOverlap);
        if (!gameObject->IsActive())
        {
            ImGui::PopStyleColor();
        }
        const std::string parentPath = ParentPath(*gameObject);
        if (!parentPath.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::SetTooltip("%s", (parentPath + "/" + gameObject->GetName()).c_str());
        }

        DrawItemInteractions(context, *gameObject);
        if (renaming)
        {
            DrawRename(*gameObject);
        }
        else if (!parentPath.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", parentPath.c_str());
        }
        ImGui::PopID();
    }

    if (matches == 0)
    {
        ImGui::TextDisabled("No objects match \"%s\"", m_Search.c_str());
    }
}

void HierarchyPanel::DrawItemInteractions(EditorContext& context, GameObject& gameObject)
{
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        m_PressedId = gameObject.GetId();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        context.Select(&gameObject);
        context.RequestFocus(gameObject.GetId());
        m_PressedId = 0;
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
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorUtils::kAssetPathPayload))
        {
            const std::string path(static_cast<const char*>(payload->Data), payload->DataSize);
            if (std::filesystem::path(path).extension() == ".prefab")
            {
                m_InstantiatePrefab = path;
                m_InstantiateParentId = gameObject.GetId();
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Rename", "F2"))
        {
            context.Select(&gameObject);
            BeginRename(gameObject);
        }
        if (ImGui::MenuItem("Create Child"))
        {
            m_CreateRequested = true;
            m_CreateParentId = gameObject.GetId();
        }
        if (ImGui::MenuItem("Save as Prefab"))
        {
            m_SavePrefabId = gameObject.GetId();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete"))
        {
            m_DeleteId = gameObject.GetId();
        }
        ImGui::EndPopup();
    }
}

void HierarchyPanel::BeginRename(const GameObject& gameObject)
{
    m_RenamingId = gameObject.GetId();
    m_RenameBuffer = gameObject.GetName();
    m_RenameFocus = true;
    m_PressedId = 0;
}

void HierarchyPanel::DrawRename(GameObject& gameObject)
{
    ImGui::SameLine();
    if (m_RenameFocus)
    {
        ImGui::SetKeyboardFocusHere();
        m_RenameFocus = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##RenameObject", &m_RenameBuffer, ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemDeactivated())
    {
        const size_t first = m_RenameBuffer.find_first_not_of(" \t");
        const size_t last = m_RenameBuffer.find_last_not_of(" \t");
        const std::string name = first == std::string::npos ? std::string() : m_RenameBuffer.substr(first, last - first + 1);
        if (!name.empty() && name != gameObject.GetName() && !ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            gameObject.SetName(name);
        }
        m_RenamingId = 0;
        m_RenameBuffer.clear();
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
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorUtils::kAssetPathPayload))
        {
            const std::string path(static_cast<const char*>(payload->Data), payload->DataSize);
            if (std::filesystem::path(path).extension() == ".prefab")
            {
                m_InstantiatePrefab = path;
                m_InstantiateParentId = 0;
            }
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

    if (m_SavePrefabId != 0)
    {
        if (GameObject* target = scene.GetGameObject(scene.FindGameObjectById(m_SavePrefabId)))
        {
            const std::filesystem::path file = EditorUtils::GetGameDirectory() / "Content" / "Prefabs"
                / EditorUtils::FromUtf8(target->GetName() + ".prefab");
            SceneSerializer::SavePrefab(*target, EditorUtils::ToUtf8(file));
        }
        m_SavePrefabId = 0;
    }

    if (!m_InstantiatePrefab.empty())
    {
        GameObject* parent = m_InstantiateParentId ? scene.GetGameObject(scene.FindGameObjectById(m_InstantiateParentId)) : nullptr;
        const std::filesystem::path file = EditorUtils::GetGameDirectory() / EditorUtils::FromUtf8(m_InstantiatePrefab);
        if (GameObject* created = SceneSerializer::InstantiatePrefab(scene, EditorUtils::ToUtf8(file), parent ? parent->GetTransform() : nullptr))
        {
            context.Select(created);
        }
        m_InstantiatePrefab.clear();
        m_InstantiateParentId = 0;
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
