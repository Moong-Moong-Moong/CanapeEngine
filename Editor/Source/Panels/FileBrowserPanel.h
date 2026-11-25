#pragma once

#include "Assets/AssetRefactor.h"
#include "Assets/ReferenceIndex.h"
#include "EditorContext.h"
#include "Panels/GltfImportWindow.h"

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class FileBrowserPanel
{
public:
    static constexpr const char* kName = "Content Browser";
    static constexpr const char* kLegacyName = "File Browser";

    struct Callbacks
    {
        std::function<bool()> prepareScene;
        std::function<void(const RefactorPlan&, bool movedOnDisk, bool sceneChecked)> afterApply;
    };

    void Draw(EditorContext& context);
    std::string RunRefactorCommand(EditorContext& context, bool move, const std::string& assetPath, const std::string& argument);
    std::string RunDeleteCommand(EditorContext& context, const std::string& assetPath);
    void QueueExternalDrop(std::vector<std::filesystem::path> files);
    void SetCallbacks(Callbacks callbacks) { m_Callbacks = std::move(callbacks); }
    void SetMessage(std::string message) { m_Message = std::move(message); }

private:
    enum class SourceKind
    {
        ScriptComponent,
        FsmState,
    };

    void DrawDirectoryTree(EditorContext& context, const std::filesystem::path& directory);
    void DrawContentsHeader();
    void DrawDirectoryContents(EditorContext& context);
    void DrawViewOptions();
    // One grid cell: large icon with the name centred under it. Returns true when clicked.
    static bool DrawGridCell(ImTextureID icon, const std::string& label, bool selected, float width, float height, float iconSize);
    void DrawEntryMenu(EditorContext& context, const std::filesystem::path& path, bool directory);
    void DrawFolderMenuItems(EditorContext& context, const std::filesystem::path& folder, bool showRename);
    void DrawMoveHereItem(EditorContext& context, const std::filesystem::path& folder, const char* label);
    // Makes the last item accept the files / folders being dragged in this browser and move them into `folder`.
    void DrawMoveDropTarget(const std::filesystem::path& folder);
    void DrawBatchPopup(EditorContext& context);
    void DrawNewSourcePopup();
    void DrawDeleteSourcePopup(EditorContext& context);
    void DrawPlanPopup(EditorContext& context);
    void DrawReferencesWindow(EditorContext& context);
    void DrawMissingWindow(EditorContext& context);
    void CreateMaterial(EditorContext& context);

    void EnsureIndex();
    void MigrateLegacyWindowSettings();
    void ProcessExternalDrops();
    bool CanEdit(const std::filesystem::path& path) const;
    bool CanCreateIn(const std::filesystem::path& folder) const;
    std::string ToAssetPath(const std::filesystem::path& path) const;

    void CreateFolder(const std::filesystem::path& parent);
    void BeginRename(const std::filesystem::path& path);
    void RenameTrace(const char* event) const;
    void CommitRename(EditorContext& context);
    void CancelRename();
    void BeginMove(std::vector<std::filesystem::path> paths);
    void MoveTo(EditorContext& context, std::vector<std::filesystem::path> sources, const std::filesystem::path& folder);
    void ShowReferences(const std::filesystem::path& path);

    // Copy / paste / duplicate of files and folders under Content. Pasting next to an existing name adds " (n)".
    void CopyAsset(const std::filesystem::path& path);
    std::string ValidatePaste(const std::filesystem::path& folder) const;
    void PasteInto(EditorContext& context, const std::filesystem::path& folder);
    void DuplicateAsset(EditorContext& context, const std::filesystem::path& path);
    void DrawPasteItem(EditorContext& context, const std::filesystem::path& folder, const char* label);
    void RequestDelete(EditorContext& context, const std::filesystem::path& path);
    void RequestDeletePaths(EditorContext& context, const std::vector<std::filesystem::path>& paths);
    void RequestPlan(EditorContext& context, const std::function<RefactorPlan()>& build, bool needsScene);
    bool ApplyPlan(EditorContext& context, const RefactorPlan& plan, bool sceneChecked);
    std::string OpenSceneDeleteError(EditorContext& context, const std::string& assetPath) const;

    // Multi-selection in the content list: Ctrl+click toggles, Shift+click selects a range, Ctrl+A selects all.
    // Holds files and folders of m_CurrentDirectory; the inspector shows the primary file (EditorContext's asset).
    bool IsSelected(const std::filesystem::path& path) const;
    void SetSelection(EditorContext& context, std::vector<std::filesystem::path> paths, const std::filesystem::path& primary);
    void HandleEntryClick(EditorContext& context, const std::filesystem::path& path);
    void SyncSelection(EditorContext& context);
    std::filesystem::path GetPrimarySelection(const EditorContext& context) const;
    // The selection when `path` is part of a multi-selection, otherwise just `path`; only items under Content.
    std::vector<std::filesystem::path> GetActionTargets(const std::filesystem::path& path) const;

    // Moves / deletes of several items: one confirmation listing all of them, then each is planned and applied in turn.
    struct BatchRequest
    {
        bool deleting = false;
        std::string targetFolder; // game-relative, moves only
        std::vector<std::string> assets;
        std::vector<RefactorPlan> preview; // planned when requested; re-planned item by item when applied
        bool sceneChecked = false;
    };
    void RequestBatch(EditorContext& context, bool deleting, const std::vector<std::filesystem::path>& paths,
        const std::filesystem::path& folder);
    void ApplyBatch(EditorContext& context, const BatchRequest& request);

    std::filesystem::path m_CurrentDirectory;

    // C++ source create / delete
    bool m_OpenNewSource = false;
    SourceKind m_NewSourceKind = SourceKind::ScriptComponent;
    std::string m_NewSourceName;
    bool m_OpenDeleteSource = false;
    std::filesystem::path m_DeleteSource; // Without extension.
    std::string m_Message;

    Callbacks m_Callbacks;
    ReferenceIndex m_Index;

    std::filesystem::path m_RenamingPath;
    std::string m_RenameBuffer;
    bool m_RenameFocus = false;
    std::filesystem::path m_RenameArmedPath;
    double m_RenameArmTime = 0.0;

    std::vector<std::filesystem::path> m_MoveSources;
    std::filesystem::path m_CopySource;

    std::vector<std::filesystem::path> m_Selection;
    std::filesystem::path m_SelectionAnchor;
    std::filesystem::path m_SyncedAsset;                // EditorContext's asset as last seen, to notice outside changes
    std::vector<std::filesystem::path> m_VisibleEntries; // content list order (last frame), for Shift ranges and Ctrl+A
    std::vector<std::filesystem::path> m_DragSources;    // what a drag started in this browser carries
    std::vector<std::filesystem::path> m_DropMoveSources;
    std::filesystem::path m_DropMoveTarget;

    std::optional<BatchRequest> m_PendingBatch;
    bool m_OpenBatchPopup = false;
    float m_GridIconSize = 0.0f; // live slider value, saved to EditorSettings when the drag ends

    std::optional<RefactorPlan> m_PendingPlan;
    bool m_PendingSceneChecked = false;
    bool m_OpenPlanPopup = false;

    bool m_ShowReferences = false;
    std::string m_ReferencesTarget;
    std::vector<std::string> m_References;

    bool m_ShowMissing = false;
    std::vector<ReferenceIndex::MissingReference> m_Missing;
    std::vector<std::string> m_RedirectTargets;

    std::vector<std::filesystem::path> m_DroppedFiles;
    std::vector<std::filesystem::path> m_PendingImports;
    bool m_SettingsMigrated = false;

    GltfImportWindow m_GltfImport;
};
