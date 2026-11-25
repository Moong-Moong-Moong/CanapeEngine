#include "Panels/HierarchyPanel.h"
#include "EditorUtils.h"
#include "Icons/EditorIcons.h"

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

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && m_RenamingId == 0 && !ImGui::GetIO().WantTextInput)
    {
        GameObject* selected = context.GetSelected(scene);
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && selected)
        {
            BeginRename(*selected);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && selected)
        {
            RequestDelete(*selected);
        }
        if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_C, false) && selected)
            {
                m_CopyId = selected->GetId();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_D, false) && selected)
            {
                m_DuplicateId = selected->GetId();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V, false) && HasClipboard())
            {
                // Pasted next to the selection (same parent), or at the scene root when nothing is selected.
                const TransformComponent* parent = selected ? selected->GetTransform()->GetParent() : nullptr;
                const GameObject* parentObject = parent ? parent->GetOwnerObject() : nullptr;
                m_Paste = { true, parentObject ? parentObject->GetId() : 0 };
            }
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

    DrawDeleteConfirm(*scene);
    if (!m_UndoLabel.empty())
    {
        context.SetUndoLabel(std::exchange(m_UndoLabel, {}));
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
    // Every row reserves exactly one arrow's width (no extra frame padding), with or without children,
    // so a row never shifts when it gains or loses children.
    const std::string padding = EditorIcons::GetLabelPadding();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(gameObject.GetId())),
        flags, "%s%s", padding.c_str(), renaming ? "" : gameObject.GetName().c_str());
    EditorIcons::DrawOnTreeNodeLabel(EditorIcons::Get().GetGameObject(), false);
    ImGui::PopStyleVar();
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

void HierarchyPanel::RequestDelete(const GameObject& gameObject)
{
    m_ConfirmDeleteId = gameObject.GetId();
    m_OpenDeleteConfirm = true;
}

void HierarchyPanel::DrawDeleteConfirm(Scene& scene)
{
    constexpr const char* kPopup = "Delete GameObject?";
    if (m_OpenDeleteConfirm)
    {
        ImGui::OpenPopup(kPopup);
        m_OpenDeleteConfirm = false;
    }
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    GameObject* target = scene.GetGameObject(scene.FindGameObjectById(m_ConfirmDeleteId));
    if (!target)
    {
        m_ConfirmDeleteId = 0;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    std::vector<GameObject*> subtree;
    CollectSubtree(*target->GetTransform(), subtree);
    ImGui::Text("Delete \"%s\"?", target->GetName().c_str());
    if (subtree.size() > 1)
    {
        ImGui::Text("%zu child object(s) will be deleted with it.", subtree.size() - 1);
    }
    ImGui::TextDisabled("Ctrl+Z restores it.");
    ImGui::Separator();

    const bool confirm = ImGui::Button("Delete", ImVec2(100.0f, 0.0f))
        || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    ImGui::TextDisabled("Enter: delete   Esc: cancel");
    if (confirm)
    {
        m_DeleteId = m_ConfirmDeleteId;
    }
    if (confirm || cancel)
    {
        m_ConfirmDeleteId = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
        const ImVec2 labelStart = ImGui::GetCursorScreenPos();
        const std::string label = EditorIcons::GetLabelPadding() + (renaming ? std::string() : gameObject->GetName()) + "##Row";
        ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowOverlap);
        EditorIcons::DrawAtLabelStart(EditorIcons::Get().GetGameObject(), labelStart);
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
        ImGui::Separator();
        if (ImGui::MenuItem("Copy", "Ctrl+C"))
        {
            m_CopyId = gameObject.GetId();
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, HasClipboard()))
        {
            const TransformComponent* parent = gameObject.GetTransform()->GetParent();
            const GameObject* parentObject = parent ? parent->GetOwnerObject() : nullptr;
            m_Paste = { true, parentObject ? parentObject->GetId() : 0 };
        }
        if (ImGui::MenuItem("Paste As Child", nullptr, false, HasClipboard()))
        {
            m_Paste = { true, gameObject.GetId() };
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        {
            m_DuplicateId = gameObject.GetId();
        }
        ImGui::Separator();
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
        if (ImGui::MenuItem("Delete", "Del"))
        {
            RequestDelete(gameObject);
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
            m_UndoLabel = "Rename " + gameObject.GetName();
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
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, HasClipboard()))
        {
            m_Paste = { true, 0 };
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
            context.SetUndoLabel("Reparent " + child->GetName());
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
        context.SetUndoLabel("Create GameObject");
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
            context.SetUndoLabel("Instantiate " + created->GetName());
        }
        m_InstantiatePrefab.clear();
        m_InstantiateParentId = 0;
    }

    if (m_CopyId != 0)
    {
        if (GameObject* target = scene.GetGameObject(scene.FindGameObjectById(m_CopyId)))
        {
            CopyToClipboard(*target);
        }
        m_CopyId = 0;
    }

    if (m_DuplicateId != 0)
    {
        // Duplicate = copy + paste next to the original, without replacing what the clipboard held.
        if (GameObject* target = scene.GetGameObject(scene.FindGameObjectById(m_DuplicateId)))
        {
            const std::string previousClipboard = m_Clipboard;
            const Matrix4 previousWorld = m_ClipboardWorld;
            const uint64_t previousParentId = m_ClipboardParentId;
            const std::string previousSceneKey = m_ClipboardSceneKey;
            CopyToClipboard(*target);
            TransformComponent* parent = target->GetTransform()->GetParent();
            if (GameObject* created = PasteClipboard(scene, parent ? parent->GetOwnerObject() : nullptr))
            {
                context.Select(created);
                context.SetUndoLabel("Duplicate " + target->GetName());
            }
            m_Clipboard = previousClipboard;
            m_ClipboardWorld = previousWorld;
            m_ClipboardParentId = previousParentId;
            m_ClipboardSceneKey = previousSceneKey;
        }
        m_DuplicateId = 0;
    }

    if (m_Paste.pending)
    {
        GameObject* parent = m_Paste.parentId ? scene.GetGameObject(scene.FindGameObjectById(m_Paste.parentId)) : nullptr;
        if (m_Paste.parentId == 0 || parent)
        {
            if (GameObject* created = PasteClipboard(scene, parent))
            {
                context.Select(created);
                context.SetUndoLabel("Paste " + created->GetName());
            }
        }
        m_Paste = {};
    }

    if (m_DeleteId != 0)
    {
        if (GameObject* target = scene.GetGameObject(scene.FindGameObjectById(m_DeleteId)))
        {
            context.SetUndoLabel("Delete " + target->GetName());
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

void HierarchyPanel::CopyToClipboard(GameObject& gameObject)
{
    m_Clipboard = SceneSerializer::SaveTreeToString(gameObject);
    m_ClipboardWorld = gameObject.GetTransform()->GetLocalToWorldMatrix();
    const TransformComponent* parent = gameObject.GetTransform()->GetParent();
    const GameObject* parentObject = parent ? parent->GetOwnerObject() : nullptr;
    m_ClipboardParentId = parentObject ? parentObject->GetId() : 0;
    m_ClipboardSceneKey = m_SceneKey;
}

GameObject* HierarchyPanel::PasteClipboard(Scene& scene, GameObject* parent)
{
    if (m_Clipboard.empty())
    {
        return nullptr;
    }

    const uint64_t parentId = parent ? parent->GetId() : 0;
    if (parentId == m_ClipboardParentId && m_ClipboardSceneKey == m_SceneKey)
    {
        // Same parent as the original: the serialized local transform already puts the copy on top of it.
        GameObject* created = SceneSerializer::InstantiateFromString(scene, m_Clipboard, parent ? parent->GetTransform() : nullptr);
        if (created)
        {
            created->SetName(MakeUniqueName(scene, *created));
        }
        return created;
    }

    // Different parent: restore the copied world pose at the root (where local == world), then reparent keeping it.
    GameObject* created = SceneSerializer::InstantiateFromString(scene, m_Clipboard, nullptr);
    if (!created)
    {
        return nullptr;
    }
    TransformComponent* transform = created->GetTransform();
    DirectX::XMVECTOR scale, rotation, translation;
    if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, m_ClipboardWorld.ToXM()))
    {
        transform->SetPosition(Vector3::FromXM(translation));
        transform->SetRotation(Quaternion::FromXM(rotation).Normalized());
        transform->SetScale(Vector3::FromXM(scale));
    }
    if (parent)
    {
        EditorUtils::SetParentKeepWorld(transform, parent->GetTransform());
    }
    created->SetName(MakeUniqueName(scene, *created));
    return created;
}

std::string HierarchyPanel::MakeUniqueName(Scene& scene, const GameObject& created)
{
    // Siblings of the new object: children of its parent, or the scene's root objects.
    std::vector<std::string> taken;
    if (const TransformComponent* parent = created.GetTransform()->GetParent())
    {
        for (const TransformComponent* child : parent->GetChildren())
        {
            const GameObject* owner = child->GetOwnerObject();
            if (owner && owner != &created && !owner->IsPendingDestroy())
            {
                taken.push_back(owner->GetName());
            }
        }
    }
    else
    {
        for (const GameObject* gameObject : scene.GetAllGameObjects())
        {
            if (gameObject != &created && !gameObject->IsPendingDestroy() && !gameObject->GetTransform()->GetParent())
            {
                taken.push_back(gameObject->GetName());
            }
        }
    }

    const std::string& name = created.GetName();
    if (std::find(taken.begin(), taken.end(), name) == taken.end())
    {
        return name;
    }

    // "Cube (2)" duplicates as "Cube (3)", not "Cube (2) (1)".
    std::string base = name;
    if (const size_t open = name.rfind(" ("); open != std::string::npos && name.back() == ')'
        && open + 2 < name.size() - 1
        && std::all_of(name.begin() + open + 2, name.end() - 1, [](char c) { return c >= '0' && c <= '9'; }))
    {
        base = name.substr(0, open);
    }
    for (int index = 1;; ++index)
    {
        std::string candidate = base + " (" + std::to_string(index) + ")";
        if (std::find(taken.begin(), taken.end(), candidate) == taken.end())
        {
            return candidate;
        }
    }
}
