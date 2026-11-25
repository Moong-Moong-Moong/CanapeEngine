#pragma once

#include "EditorContext.h"

#include <cstdint>
#include <string>
#include <vector>

class HierarchyPanel
{
public:
    static constexpr const char* kName = "Hierarchy";

    void Draw(EditorContext& context, Canape::Scene* scene);

private:
    struct Reparent
    {
        uint64_t childId = 0; // the dragged object; the whole selection moves when it is part of a multi-selection
        uint64_t parentId = 0;
        bool pending = false;
    };

    void DrawNode(EditorContext& context, Canape::GameObject& gameObject);
    // Deletes the object, or the whole selection when the object is part of a multi-selection.
    void RequestDelete(EditorContext& context, Canape::Scene& scene, const Canape::GameObject& gameObject);
    void RequestDeleteSelection(EditorContext& context, Canape::Scene& scene);
    void HandleClick(EditorContext& context, Canape::Scene& scene, uint64_t clickedId);
    void DrawDeleteConfirm(Canape::Scene& scene);
    void DrawSearchResults(EditorContext& context, Canape::Scene& scene);
    void DrawItemInteractions(EditorContext& context, Canape::GameObject& gameObject);
    void DrawRename(Canape::GameObject& gameObject);
    void DrawBackground(EditorContext& context);
    void ApplyPendingActions(EditorContext& context, Canape::Scene& scene);

    void BeginRename(const Canape::GameObject& gameObject);

    // Copy / paste / duplicate. The clipboard holds the copied tree in prefab format, so it survives deleting the
    // original and works across scenes.
    struct PasteRequest
    {
        bool pending = false;
        uint64_t parentId = 0; // 0 = scene root
    };
    // One copied object tree. Copying several objects keeps one per top-most selected object, in hierarchy order.
    struct ClipboardItem
    {
        std::string tree;
        Canape::Matrix4 world = Canape::Matrix4::Identity;
        uint64_t parentId = 0;
    };
    static ClipboardItem MakeClipboardItem(Canape::GameObject& gameObject);
    void CopyToClipboard(const std::vector<Canape::GameObject*>& gameObjects);
    Canape::GameObject* PasteItem(Canape::Scene& scene, const ClipboardItem& item, const std::string& sceneKey,
        Canape::GameObject* parent);
    // The selection's top-most objects when `gameObject` is part of a multi-selection, otherwise just `gameObject`.
    static std::vector<uint64_t> ActionTargets(EditorContext& context, Canape::Scene& scene, const Canape::GameObject& gameObject);
    static std::string MakeUniqueName(Canape::Scene& scene, const Canape::GameObject& created);
    bool HasClipboard() const { return !m_Clipboard.empty(); }

    Reparent m_Reparent;
    uint64_t m_SavePrefabId = 0;
    std::string m_InstantiatePrefab;
    uint64_t m_InstantiateParentId = 0;
    uint64_t m_PressedId = 0;
    std::vector<uint64_t> m_DeleteIds;
    std::vector<uint64_t> m_ConfirmDeleteIds;
    bool m_OpenDeleteConfirm = false;
    uint64_t m_CreateParentId = 0;
    bool m_CreateRequested = false;

    uint64_t m_SelectionAnchor = 0;       // Shift+click selects the visible rows between this and the clicked row
    std::vector<uint64_t> m_VisibleOrder; // rows in draw order (last frame), for Shift+click ranges and Ctrl+A
    std::vector<uint64_t> m_DrawOrder;    // rows drawn this frame

    std::string m_Search;
    std::string m_SceneKey;

    uint64_t m_RenamingId = 0;
    std::string m_RenameBuffer;
    std::string m_UndoLabel; // set by edits made where the context is not at hand (rename)
    bool m_RenameFocus = false;

    std::vector<ClipboardItem> m_Clipboard;
    std::string m_ClipboardSceneKey; // object ids only mean the same thing inside the scene they were copied from
    std::vector<uint64_t> m_CopyIds;
    std::vector<uint64_t> m_DuplicateIds;
    PasteRequest m_Paste;
};
