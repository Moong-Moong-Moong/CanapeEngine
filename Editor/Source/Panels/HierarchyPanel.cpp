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

    // Every live object in hierarchy order (parents before children, siblings in order).
    void CollectHierarchyOrder(TransformComponent& transform, std::vector<uint64_t>& result)
    {
        if (GameObject* owner = transform.GetOwnerObject(); owner && !owner->IsPendingDestroy())
        {
            result.push_back(owner->GetId());
            for (TransformComponent* child : transform.GetChildren())
            {
                CollectHierarchyOrder(*child, result);
            }
        }
    }

    std::vector<uint64_t> HierarchyOrder(Scene& scene)
    {
        std::vector<uint64_t> order;
        for (GameObject* gameObject : scene.GetAllGameObjects())
        {
            if (!gameObject->IsPendingDestroy() && !gameObject->GetTransform()->GetParent())
            {
                CollectHierarchyOrder(*gameObject->GetTransform(), order);
            }
        }
        return order;
    }

    void SortByHierarchyOrder(Scene& scene, std::vector<GameObject*>& objects)
    {
        const std::vector<uint64_t> order = HierarchyOrder(scene);
        const auto rank = [&](const GameObject* gameObject)
        {
            return static_cast<size_t>(std::find(order.begin(), order.end(), gameObject->GetId()) - order.begin());
        };
        std::stable_sort(objects.begin(), objects.end(), [&](const GameObject* lhs, const GameObject* rhs) { return rank(lhs) < rank(rhs); });
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
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && selected && context.GetSelectionCount() <= 1)
        {
            BeginRename(*selected);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && context.GetSelectionCount() > 0)
        {
            RequestDeleteSelection(context, *scene);
        }
        if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_A, false) && !m_VisibleOrder.empty())
            {
                // Every row currently shown (expanded tree rows, or the search results).
                const uint64_t primary = context.IsSelected(context.GetSelectedId()) ? context.GetSelectedId() : m_VisibleOrder.front();
                context.SetSelection(m_VisibleOrder, primary);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_C, false) && selected)
            {
                m_CopyIds = ActionTargets(context, *scene, *selected);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_D, false) && selected)
            {
                m_DuplicateIds = ActionTargets(context, *scene, *selected);
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

    m_DrawOrder.clear();
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

    m_VisibleOrder.swap(m_DrawOrder);

    DrawBackground(context);

    if (m_PressedId != 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        HandleClick(context, *scene, m_PressedId);
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
    const bool selected = context.IsSelected(gameObject.GetId());
    const bool renaming = m_RenamingId == gameObject.GetId();
    m_DrawOrder.push_back(gameObject.GetId());

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

void HierarchyPanel::HandleClick(EditorContext& context, Scene& scene, uint64_t clickedId)
{
    GameObject* clicked = scene.GetGameObject(scene.FindGameObjectById(clickedId));
    if (!clicked || clicked->IsPendingDestroy())
    {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && m_SelectionAnchor != 0)
    {
        // Shift: the visible rows from the anchor to the clicked row. Ctrl+Shift adds them to the selection.
        const auto anchor = std::find(m_VisibleOrder.begin(), m_VisibleOrder.end(), m_SelectionAnchor);
        const auto target = std::find(m_VisibleOrder.begin(), m_VisibleOrder.end(), clickedId);
        if (anchor != m_VisibleOrder.end() && target != m_VisibleOrder.end())
        {
            std::vector<uint64_t> ids = io.KeyCtrl ? context.GetSelectedIds() : std::vector<uint64_t>{};
            const auto first = std::min(anchor, target);
            const auto last = std::max(anchor, target);
            ids.insert(ids.end(), first, last + 1);
            context.SetSelection(std::move(ids), clickedId);
            return;
        }
    }

    if (io.KeyCtrl)
    {
        context.ToggleSelect(clicked);
    }
    else
    {
        context.Select(clicked);
    }
    m_SelectionAnchor = clickedId;
}

void HierarchyPanel::RequestDelete(EditorContext& context, Scene& scene, const GameObject& gameObject)
{
    if (context.IsSelected(gameObject.GetId()) && context.GetSelectionCount() > 1)
    {
        RequestDeleteSelection(context, scene);
        return;
    }
    m_ConfirmDeleteIds = { gameObject.GetId() };
    m_OpenDeleteConfirm = true;
}

void HierarchyPanel::RequestDeleteSelection(EditorContext& context, Scene& scene)
{
    // Children of selected objects go with their parent, so only the top-most selected objects are listed.
    std::vector<GameObject*> roots = context.GetSelectedRoots(&scene);
    SortByHierarchyOrder(scene, roots);
    m_ConfirmDeleteIds.clear();
    for (const GameObject* gameObject : roots)
    {
        m_ConfirmDeleteIds.push_back(gameObject->GetId());
    }
    m_OpenDeleteConfirm = !m_ConfirmDeleteIds.empty();
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

    std::vector<GameObject*> targets;
    for (const uint64_t id : m_ConfirmDeleteIds)
    {
        GameObject* target = scene.GetGameObject(scene.FindGameObjectById(id));
        if (target && !target->IsPendingDestroy())
        {
            targets.push_back(target);
        }
    }
    if (targets.empty())
    {
        m_ConfirmDeleteIds.clear();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    size_t childCount = 0;
    for (GameObject* target : targets)
    {
        std::vector<GameObject*> subtree;
        CollectSubtree(*target->GetTransform(), subtree);
        childCount += subtree.size() - 1;
    }
    if (targets.size() == 1)
    {
        ImGui::Text("Delete \"%s\"?", targets.front()->GetName().c_str());
    }
    else
    {
        ImGui::Text("Delete %zu objects?", targets.size());
        constexpr size_t kListed = 10;
        for (size_t i = 0; i < targets.size() && i < kListed; ++i)
        {
            ImGui::BulletText("%s", targets[i]->GetName().c_str());
        }
        if (targets.size() > kListed)
        {
            ImGui::TextDisabled("...and %zu more", targets.size() - kListed);
        }
    }
    if (childCount > 0)
    {
        ImGui::Text("%zu child object(s) will be deleted with %s.", childCount, targets.size() == 1 ? "it" : "them");
    }
    ImGui::TextDisabled("%s", targets.size() == 1 ? "Ctrl+Z restores it." : "Ctrl+Z restores them.");
    ImGui::Separator();

    const bool confirm = ImGui::Button("Delete", ImVec2(100.0f, 0.0f))
        || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    ImGui::TextDisabled("Enter: delete   Esc: cancel");
    if (confirm)
    {
        m_DeleteIds = m_ConfirmDeleteIds;
    }
    if (confirm || cancel)
    {
        m_ConfirmDeleteIds.clear();
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
        const bool selected = context.IsSelected(gameObject->GetId());
        m_DrawOrder.push_back(gameObject->GetId());
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
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()
        && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
    {
        context.Select(&gameObject);
        context.RequestFocus(gameObject.GetId());
        m_SelectionAnchor = gameObject.GetId();
        m_PressedId = 0;
    }

    if (ImGui::BeginDragDropSource())
    {
        m_PressedId = 0;
        // Dragging a selected row drags the whole selection; the payload names the row, the drop reads the selection.
        const uint64_t id = gameObject.GetId();
        ImGui::SetDragDropPayload(EditorUtils::kGameObjectPayload, &id, sizeof(id));
        const size_t count = context.IsSelected(id) ? context.GetSelectionCount() : 1;
        if (count > 1)
        {
            ImGui::Text("%s and %zu more", gameObject.GetName().c_str(), count - 1);
        }
        else
        {
            ImGui::TextUnformatted(gameObject.GetName().c_str());
        }
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
        const size_t selectedCount = context.IsSelected(gameObject.GetId()) ? context.GetSelectionCount() : 1;
        const std::string countSuffix = selectedCount > 1 ? " " + std::to_string(selectedCount) + " Objects" : std::string();
        if (ImGui::MenuItem(("Copy" + countSuffix).c_str(), "Ctrl+C") && gameObject.GetScene())
        {
            m_CopyIds = ActionTargets(context, *gameObject.GetScene(), gameObject);
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
        if (ImGui::MenuItem(("Duplicate" + countSuffix).c_str(), "Ctrl+D") && gameObject.GetScene())
        {
            m_DuplicateIds = ActionTargets(context, *gameObject.GetScene(), gameObject);
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
        const size_t count = context.IsSelected(gameObject.GetId()) ? context.GetSelectionCount() : 1;
        const std::string deleteLabel = count > 1 ? "Delete " + std::to_string(count) + " Objects" : std::string("Delete");
        if (ImGui::MenuItem(deleteLabel.c_str(), "Del") && gameObject.GetScene())
        {
            RequestDelete(context, *gameObject.GetScene(), gameObject);
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

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
    {
        context.ClearSelection();
        m_SelectionAnchor = 0;
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
            // A dragged multi-selection moves its top-most objects (children follow their parents), in hierarchy order.
            std::vector<GameObject*> children;
            if (context.IsSelected(child->GetId()) && context.GetSelectionCount() > 1)
            {
                children = context.GetSelectedRoots(&scene);
                SortByHierarchyOrder(scene, children);
            }
            else
            {
                children.push_back(child);
            }

            TransformComponent* parentTransform = parent ? parent->GetTransform() : nullptr;
            size_t moved = 0;
            for (GameObject* gameObject : children)
            {
                // Skips the drop target itself and its ancestors (an object cannot become its own descendant).
                if (EditorUtils::SetParentKeepWorld(gameObject->GetTransform(), parentTransform))
                {
                    ++moved;
                }
            }
            if (moved == 1 && children.size() == 1)
            {
                context.SetUndoLabel("Reparent " + child->GetName());
            }
            else if (moved > 0)
            {
                context.SetUndoLabel("Reparent " + std::to_string(moved) + " Objects");
            }
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

    const auto resolve = [&scene](const std::vector<uint64_t>& ids)
    {
        std::vector<GameObject*> objects;
        for (const uint64_t id : ids)
        {
            GameObject* gameObject = scene.GetGameObject(scene.FindGameObjectById(id));
            if (gameObject && !gameObject->IsPendingDestroy())
            {
                objects.push_back(gameObject);
            }
        }
        return objects;
    };

    if (!m_CopyIds.empty())
    {
        const std::vector<GameObject*> targets = resolve(m_CopyIds);
        if (!targets.empty())
        {
            CopyToClipboard(targets);
        }
        m_CopyIds.clear();
    }

    if (!m_DuplicateIds.empty())
    {
        // Duplicate = copy + paste each object next to its original, without touching the clipboard.
        const std::vector<GameObject*> targets = resolve(m_DuplicateIds);
        std::vector<ClipboardItem> items;
        for (GameObject* target : targets)
        {
            items.push_back(MakeClipboardItem(*target));
        }
        std::vector<uint64_t> createdIds;
        for (size_t i = 0; i < targets.size(); ++i)
        {
            TransformComponent* parent = targets[i]->GetTransform()->GetParent();
            if (GameObject* created = PasteItem(scene, items[i], m_SceneKey, parent ? parent->GetOwnerObject() : nullptr))
            {
                createdIds.push_back(created->GetId());
            }
        }
        if (!createdIds.empty())
        {
            const uint64_t primary = createdIds.back();
            context.SetSelection(std::move(createdIds), primary);
            context.SetUndoLabel(targets.size() == 1 ? "Duplicate " + targets.front()->GetName()
                : "Duplicate " + std::to_string(targets.size()) + " Objects");
        }
        m_DuplicateIds.clear();
    }

    if (m_Paste.pending)
    {
        GameObject* parent = m_Paste.parentId ? scene.GetGameObject(scene.FindGameObjectById(m_Paste.parentId)) : nullptr;
        if (m_Paste.parentId == 0 || parent)
        {
            // Every copied tree goes under the same parent; the pasted objects become the selection.
            std::vector<uint64_t> createdIds;
            std::string lastName;
            for (const ClipboardItem& item : m_Clipboard)
            {
                if (GameObject* created = PasteItem(scene, item, m_ClipboardSceneKey, parent))
                {
                    createdIds.push_back(created->GetId());
                    lastName = created->GetName();
                }
            }
            if (!createdIds.empty())
            {
                const size_t count = createdIds.size();
                const uint64_t primary = createdIds.back();
                context.SetSelection(std::move(createdIds), primary);
                context.SetUndoLabel(count == 1 ? "Paste " + lastName : "Paste " + std::to_string(count) + " Objects");
            }
        }
        m_Paste = {};
    }

    if (!m_DeleteIds.empty())
    {
        // Collect every subtree first: removing an object must not invalidate the next target's lookup.
        std::vector<GameObject*> targets;
        for (const uint64_t id : m_DeleteIds)
        {
            GameObject* target = scene.GetGameObject(scene.FindGameObjectById(id));
            if (target && !target->IsPendingDestroy())
            {
                targets.push_back(target);
            }
        }
        std::vector<GameObject*> doomed;
        for (GameObject* target : targets)
        {
            std::vector<GameObject*> subtree;
            CollectSubtree(*target->GetTransform(), subtree);
            for (GameObject* gameObject : subtree)
            {
                if (std::find(doomed.begin(), doomed.end(), gameObject) == doomed.end())
                {
                    doomed.push_back(gameObject);
                }
            }
        }

        if (!targets.empty())
        {
            context.SetUndoLabel(targets.size() == 1 ? "Delete " + targets.front()->GetName()
                : "Delete " + std::to_string(targets.size()) + " Objects");

            // Whatever stays selected after the delete (objects outside the deleted subtrees) keeps its selection.
            std::vector<uint64_t> remaining;
            for (const uint64_t id : context.GetSelectedIds())
            {
                const bool deleted = std::any_of(doomed.begin(), doomed.end(),
                    [id](const GameObject* gameObject) { return gameObject->GetId() == id; });
                if (!deleted)
                {
                    remaining.push_back(id);
                }
            }
            const uint64_t primary = context.GetSelectedId();
            context.SetSelection(std::move(remaining), primary);
        }
        for (GameObject* gameObject : doomed)
        {
            scene.RemoveGameObject(gameObject->GetHandle());
        }
        m_DeleteIds.clear();
    }
}

std::vector<uint64_t> HierarchyPanel::ActionTargets(EditorContext& context, Scene& scene, const GameObject& gameObject)
{
    if (!context.IsSelected(gameObject.GetId()) || context.GetSelectionCount() < 2)
    {
        return { gameObject.GetId() };
    }
    // Children of selected objects are copied inside their parent's tree, so only the top-most objects are listed.
    std::vector<GameObject*> roots = context.GetSelectedRoots(&scene);
    SortByHierarchyOrder(scene, roots);
    std::vector<uint64_t> ids;
    for (const GameObject* root : roots)
    {
        ids.push_back(root->GetId());
    }
    return ids;
}

HierarchyPanel::ClipboardItem HierarchyPanel::MakeClipboardItem(GameObject& gameObject)
{
    ClipboardItem item;
    item.tree = SceneSerializer::SaveTreeToString(gameObject);
    item.world = gameObject.GetTransform()->GetLocalToWorldMatrix();
    const TransformComponent* parent = gameObject.GetTransform()->GetParent();
    const GameObject* parentObject = parent ? parent->GetOwnerObject() : nullptr;
    item.parentId = parentObject ? parentObject->GetId() : 0;
    return item;
}

void HierarchyPanel::CopyToClipboard(const std::vector<GameObject*>& gameObjects)
{
    m_Clipboard.clear();
    for (GameObject* gameObject : gameObjects)
    {
        m_Clipboard.push_back(MakeClipboardItem(*gameObject));
    }
    m_ClipboardSceneKey = m_SceneKey;
}

GameObject* HierarchyPanel::PasteItem(Scene& scene, const ClipboardItem& item, const std::string& sceneKey, GameObject* parent)
{
    if (item.tree.empty())
    {
        return nullptr;
    }

    const uint64_t parentId = parent ? parent->GetId() : 0;
    if (parentId == item.parentId && sceneKey == m_SceneKey)
    {
        // Same parent as the original: the serialized local transform already puts the copy on top of it.
        GameObject* created = SceneSerializer::InstantiateFromString(scene, item.tree, parent ? parent->GetTransform() : nullptr);
        if (created)
        {
            created->SetName(MakeUniqueName(scene, *created));
        }
        return created;
    }

    // Different parent: restore the copied world pose at the root (where local == world), then reparent keeping it.
    GameObject* created = SceneSerializer::InstantiateFromString(scene, item.tree, nullptr);
    if (!created)
    {
        return nullptr;
    }
    TransformComponent* transform = created->GetTransform();
    DirectX::XMVECTOR scale, rotation, translation;
    if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, item.world.ToXM()))
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
