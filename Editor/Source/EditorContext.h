#pragma once

#include <Engine.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string>
#include <utility>

class EditorContext
{
public:
    Canape::GameObject* GetSelected(Canape::Scene* scene) const;
    void Select(const Canape::GameObject* gameObject);
    void ClearSelection() { m_SelectedId = 0; }
    uint64_t GetSelectedId() const { return m_SelectedId; }

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
    uint64_t m_PendingFocusId = 0;
    std::filesystem::path m_SelectedAsset;
    std::string m_ScenePath;
    std::string m_PendingOpenScene;
    std::string m_UndoLabel;
};
