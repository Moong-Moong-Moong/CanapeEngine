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
        uint64_t childId = 0;
        uint64_t parentId = 0;
        bool pending = false;
    };

    void DrawNode(EditorContext& context, Canape::GameObject& gameObject);
    void RequestDelete(const Canape::GameObject& gameObject);
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
    void CopyToClipboard(Canape::GameObject& gameObject);
    Canape::GameObject* PasteClipboard(Canape::Scene& scene, Canape::GameObject* parent);
    static std::string MakeUniqueName(Canape::Scene& scene, const Canape::GameObject& created);
    bool HasClipboard() const { return !m_Clipboard.empty(); }

    Reparent m_Reparent;
    uint64_t m_SavePrefabId = 0;
    std::string m_InstantiatePrefab;
    uint64_t m_InstantiateParentId = 0;
    uint64_t m_PressedId = 0;
    uint64_t m_DeleteId = 0;
    uint64_t m_ConfirmDeleteId = 0;
    bool m_OpenDeleteConfirm = false;
    uint64_t m_CreateParentId = 0;
    bool m_CreateRequested = false;

    std::string m_Search;
    std::string m_SceneKey;

    uint64_t m_RenamingId = 0;
    std::string m_RenameBuffer;
    std::string m_UndoLabel; // set by edits made where the context is not at hand (rename)
    bool m_RenameFocus = false;

    std::string m_Clipboard;
    Canape::Matrix4 m_ClipboardWorld = Canape::Matrix4::Identity;
    uint64_t m_ClipboardParentId = 0;
    std::string m_ClipboardSceneKey; // object ids only mean the same thing inside the scene they were copied from
    uint64_t m_CopyId = 0;
    uint64_t m_DuplicateId = 0;
    PasteRequest m_Paste;
};
