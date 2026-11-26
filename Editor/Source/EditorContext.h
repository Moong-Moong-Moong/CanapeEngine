#pragma once

#include <Engine.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

class EditorContext
{
public:
    // The primary selection: the object the inspector shows and the gizmo moves.
    Canape::GameObject* GetSelected(Canape::Scene* scene) const;
    // Selects only this object (clears any multi-selection).
    void Select(const Canape::GameObject* gameObject);
    void ClearSelection();
    uint64_t GetSelectedId() const { return m_SelectedId; }

    // Multi-selection (hierarchy Ctrl/Shift+click, scene view Ctrl+click). Always contains the primary when one is set.
    const std::vector<uint64_t>& GetSelectedIds() const { return m_SelectedIds; }
    bool IsSelected(uint64_t gameObjectId) const;
    size_t GetSelectionCount() const { return m_SelectedIds.size(); }
    // Adds the object, or removes it when it is already selected. The primary moves to the object (or to the most
    // recently selected remaining one when it is removed).
    void ToggleSelect(const Canape::GameObject* gameObject);
    // Replaces the selection. `primaryId` must be one of `ids` (otherwise the last id becomes the primary).
    void SetSelection(std::vector<uint64_t> ids, uint64_t primaryId);
    // Live selected objects in selection order, skipping ids that no longer exist.
    std::vector<Canape::GameObject*> GetSelectedObjects(Canape::Scene* scene) const;
    // Selected objects without a selected ancestor: moving or deleting these also covers every other selected object.
    std::vector<Canape::GameObject*> GetSelectedRoots(Canape::Scene* scene) const;

    // Asks the scene view to move its camera in front of the object (hierarchy double-click).
    void RequestFocus(uint64_t gameObjectId) { m_PendingFocusId = gameObjectId; }
    uint64_t TakeFocusRequest() { return std::exchange(m_PendingFocusId, 0); }

    const std::filesystem::path& GetSelectedAsset() const { return m_SelectedAsset; }
    void SelectAsset(std::filesystem::path path);

    const std::string& GetScenePath() const { return m_ScenePath; }
    void SetScenePath(std::string path) { m_ScenePath = std::move(path); }

    // Name for the undo step recorded when the current interaction ends ("Move Player", "Delete Enemy").
    // Panels set it where they edit the scene; unnamed edits are recorded as "Edit Scene".
    void SetUndoLabel(std::string label) { m_UndoLabel = std::move(label); }
    std::string TakeUndoLabel() { return std::exchange(m_UndoLabel, {}); }

    void RequestOpenScene(std::string path) { m_PendingOpenScene = std::move(path); }
    std::string TakePendingOpenScene() { return std::exchange(m_PendingOpenScene, {}); }

private:
    uint64_t m_SelectedId = 0;
    std::vector<uint64_t> m_SelectedIds;
    uint64_t m_PendingFocusId = 0;
    std::filesystem::path m_SelectedAsset;
    std::string m_ScenePath;
    std::string m_PendingOpenScene;
    std::string m_UndoLabel;
};
