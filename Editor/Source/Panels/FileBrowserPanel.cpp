#include "Panels/FileBrowserPanel.h"
#include "EditorUtils.h"
#include "Asset/Mesh/MeshSerializer.h"
#include "CNDebug/DebugLog.h"
#include "EditorSettings.h"
#include "Icons/EditorIcons.h"

#include <nlohmann/json.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    struct Entry
    {
        fs::path path;
        std::string name;
        bool directory = false;
    };

    std::vector<Entry> ListDirectory(const fs::path& directory, bool directoriesOnly)
    {
        std::vector<Entry> entries;
        std::error_code error;
        for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
        {
            const bool isDirectory = it->is_directory(error);
            if (directoriesOnly && !isDirectory)
            {
                continue;
            }
            entries.push_back({ it->path(), EditorUtils::ToUtf8(it->path().filename()), isDirectory });
        }

        std::sort(entries.begin(), entries.end(), [](const Entry& lhs, const Entry& rhs)
        {
            if (lhs.directory != rhs.directory)
            {
                return lhs.directory;
            }
            return lhs.name < rhs.name;
        });
        return entries;
    }

    bool HasSubdirectory(const fs::path& directory)
    {
        std::error_code error;
        for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
        {
            if (it->is_directory(error))
            {
                return true;
            }
        }
        return false;
    }

    bool IsSceneFile(const fs::path& path)
    {
        return path.extension() == ".json" && EditorUtils::ToGameRelativePath(path).starts_with("Content/Scenes/");
    }

    bool IsSourceFile(const fs::path& path)
    {
        const fs::path extension = path.extension();
        return extension == ".h" || extension == ".hpp" || extension == ".cpp" || extension == ".inl";
    }

    bool IsMeshSourceFile(const fs::path& path)
    {
        std::string extension = EditorUtils::ToUtf8(path.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
        return extension == ".gltf" || extension == ".fbx";
    }

    fs::path CnmeshPathFor(const fs::path& source)
    {
        fs::path stem = source.stem();
        if (stem == "scene" && !source.parent_path().filename().empty())
        {
            stem = source.parent_path().filename();
        }
        fs::path target = source.parent_path() / stem;
        target += Canape::MeshSerializer::StaticExtension;
        return target;
    }

    // "Source" or "Source/..." relative to the game folder, or empty when the folder is outside Game/Source.
    std::string GetSourceDirectory(const fs::path& directory)
    {
        const std::string relative = EditorUtils::ToGameRelativePath(directory);
        return relative == "Source" || relative.starts_with("Source/") ? relative : std::string();
    }
}

void FileBrowserPanel::Draw(EditorContext& context)
{
    if (m_CurrentDirectory.empty())
    {
        m_CurrentDirectory = EditorUtils::GetGameDirectory();
    }
    EnsureIndex();
    MigrateLegacyWindowSettings();
    ProcessExternalDrops();

    if (!ImGui::Begin(kName))
    {
        ImGui::End();
        DrawReferencesWindow(context);
        DrawMissingWindow(context);
        m_GltfImport.Draw();
        return;
    }

    if (!ImGui::GetDragDropPayload())
    {
        m_DragSources.clear();
    }
    SyncSelection(context);

    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && m_RenamingPath.empty() && !ImGui::GetIO().WantTextInput)
    {
        // The primary selected item (content list) or the selected asset.
        const fs::path selected = !m_Selection.empty() ? GetPrimarySelection(context) : context.GetSelectedAsset();
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && m_Selection.size() <= 1 && CanEdit(selected)
            && selected.parent_path() == m_CurrentDirectory)
        {
            BeginRename(selected);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            std::vector<fs::path> targets;
            for (const fs::path& path : m_Selection)
            {
                if (CanEdit(path))
                {
                    targets.push_back(path);
                }
            }
            if (targets.empty() && CanEdit(selected) && selected.parent_path() == m_CurrentDirectory)
            {
                targets.push_back(selected);
            }
            if (!targets.empty())
            {
                RequestDeletePaths(context, targets);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !m_MoveSources.empty())
        {
            m_MoveSources.clear();
            m_Message = "Move cancelled.";
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_A, false) && !m_VisibleEntries.empty())
            {
                const fs::path primary = IsSelected(GetPrimarySelection(context)) ? GetPrimarySelection(context) : m_VisibleEntries.front();
                SetSelection(context, m_VisibleEntries, primary);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_C, false) && CanEdit(selected))
            {
                CopyAsset(selected);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_D, false) && CanEdit(selected))
            {
                DuplicateAsset(context, selected);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V, false) && !m_CopySource.empty())
            {
                PasteInto(context, m_CurrentDirectory);
            }
        }
    }
    if (!m_RenameArmedPath.empty() && ImGui::GetTime() - m_RenameArmTime > ImGui::GetIO().MouseDoubleClickTime)
    {
        if (context.GetSelectedAsset() == m_RenameArmedPath && m_RenamingPath.empty())
        {
            BeginRename(m_RenameArmedPath);
        }
        m_RenameArmedPath.clear();
    }

    if (ImGui::BeginTable("##FileBrowserLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
        ImGui::GetContentRegionAvail()))
    {
        ImGui::TableSetupColumn("Directories", ImGuiTableColumnFlags_WidthStretch, 0.3f);
        ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("##DirectoryTree"))
        {
            DrawDirectoryTree(context, EditorUtils::GetGameDirectory());
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        // The path bar stays put; only the file list below it scrolls.
        DrawContentsHeader();
        if (ImGui::BeginChild("##DirectoryContents"))
        {
            DrawDirectoryContents(context);
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }
    DrawPlanPopup(context);
    DrawBatchPopup(context);
    ImGui::End();

    if (!m_DropMoveTarget.empty())
    {
        // Deferred to here: moving may ask to save the scene (a message box) and rebuilds the listings drawn above.
        const std::vector<fs::path> sources = std::exchange(m_DropMoveSources, {});
        const fs::path target = std::exchange(m_DropMoveTarget, {});
        MoveTo(context, sources, target);
    }

    DrawReferencesWindow(context);
    DrawMissingWindow(context);
    m_GltfImport.Draw();
}

void FileBrowserPanel::DrawDirectoryTree(EditorContext& context, const fs::path& directory)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (directory == EditorUtils::GetGameDirectory())
    {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    if (directory == m_CurrentDirectory)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!HasSubdirectory(directory))
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    const std::string name = directory == EditorUtils::GetGameDirectory()
        ? std::string("Game") : EditorUtils::ToUtf8(directory.filename());
    const std::string id = EditorUtils::ToUtf8(directory);
    const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "%s", name.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        m_CurrentDirectory = directory;
    }
    DrawMoveDropTarget(directory);
    if (ImGui::BeginPopupContextItem())
    {
        DrawFolderMenuItems(context, directory, true);
        ImGui::EndPopup();
    }

    if (open)
    {
        for (const Entry& entry : ListDirectory(directory, true))
        {
            DrawDirectoryTree(context, entry.path);
        }
        ImGui::TreePop();
    }
}

void FileBrowserPanel::DrawContentsHeader()
{
    const fs::path& root = EditorUtils::GetGameDirectory();
    const std::string location = EditorUtils::ToGameRelativePath(m_CurrentDirectory);

    ImGui::BeginDisabled(m_CurrentDirectory == root);
    if (ImGui::ArrowButton("##Up", ImGuiDir_Up))
    {
        m_CurrentDirectory = m_CurrentDirectory.parent_path();
    }
    if (m_CurrentDirectory != root)
    {
        DrawMoveDropTarget(m_CurrentDirectory.parent_path());
    }
    if (m_GridIconSize <= 0.0f)
    {
        m_GridIconSize = EditorSettings::Get().GetContentBrowserGridSize();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted(location == "." ? "Game" : ("Game/" + location).c_str());
    DrawViewOptions();
    if (!m_MoveSources.empty())
    {
        if (m_MoveSources.size() == 1)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Moving: %s", ToAssetPath(m_MoveSources.front()).c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Moving %zu items from %s", m_MoveSources.size(),
                ToAssetPath(m_MoveSources.front().parent_path()).c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel Move"))
        {
            m_MoveSources.clear();
        }
        ImGui::TextDisabled("Right-click a folder and choose Move Here. Esc cancels.");
    }
    if (!m_Message.empty())
    {
        ImGui::TextWrapped("%s", m_Message.c_str());
    }
    ImGui::Separator();
}

void FileBrowserPanel::DrawDirectoryContents(EditorContext& context)
{
    EditorIcons& icons = EditorIcons::Get();
    const bool grid = EditorSettings::Get().IsContentBrowserGrid();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float iconSize = m_GridIconSize;
    const float cellWidth = iconSize + 16.0f;
    const float cellHeight = iconSize + ImGui::GetTextLineHeight() + 12.0f;
    const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x)
        / (cellWidth + style.ItemSpacing.x)));

    fs::path nextDirectory;
    bool renameRowDrawn = false;
    int cellIndex = 0;
    std::vector<fs::path> visibleEntries;
    for (const Entry& entry : ListDirectory(m_CurrentDirectory, false))
    {
        visibleEntries.push_back(entry.path);
        if (grid && cellIndex++ % columns != 0)
        {
            ImGui::SameLine();
        }
        ImGui::PushID(entry.name.c_str());
        const ImTextureID icon = icons.GetFile(entry.path, entry.directory);

        if (entry.path == m_RenamingPath)
        {
            renameRowDrawn = true;
            if (grid)
            {
                ImGui::BeginGroup();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cellWidth - iconSize) * 0.5f);
                ImGui::Image(icon, ImVec2(iconSize, iconSize));
                ImGui::SetNextItemWidth(cellWidth);
            }
            else
            {
                ImGui::Image(icon, ImVec2(EditorIcons::GetInlineSize(), EditorIcons::GetInlineSize()));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
            }
            if (m_RenameFocus)
            {
                ImGui::SetKeyboardFocusHere();
                m_RenameFocus = false;
            }
            const bool submitted = ImGui::InputText("##Rename", &m_RenameBuffer,
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            RenameTrace(submitted ? "submitted" : ImGui::IsItemDeactivated() ? "deactivated" : "frame");
            if (submitted)
            {
                CommitRename(context);
            }
            else if (ImGui::IsItemDeactivated())
            {
                CancelRename();
            }
            if (grid)
            {
                ImGui::EndGroup();
            }
            ImGui::PopID();
            continue;
        }

        std::string label = entry.name;
        if (std::find(m_MoveSources.begin(), m_MoveSources.end(), entry.path) != m_MoveSources.end())
        {
            label += "  (moving)";
        }
        const bool selected = IsSelected(entry.path);
        bool clicked = false;
        if (grid)
        {
            clicked = DrawGridCell(icon, label, selected, cellWidth, cellHeight, iconSize);
        }
        else
        {
            if (entry.directory)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.8f, 0.4f, 1.0f));
            }
            const ImVec2 labelStart = ImGui::GetCursorScreenPos();
            clicked = ImGui::Selectable((EditorIcons::GetLabelPadding() + label).c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick);
            EditorIcons::DrawAtLabelStart(icon, labelStart);
            if (entry.directory)
            {
                ImGui::PopStyleColor();
            }
        }

        if (clicked)
        {
            // A plain click on the only selected file arms rename (slow second click), as before.
            const ImGuiIO& io = ImGui::GetIO();
            if (!entry.directory && selected && m_Selection.size() == 1 && !io.KeyCtrl && !io.KeyShift
                && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && CanEdit(entry.path))
            {
                m_RenameArmedPath = entry.path;
                m_RenameArmTime = ImGui::GetTime();
            }
            // Folders are selected here only (not in the inspector) so Delete / F2 / Ctrl+C / Ctrl+D can act on them.
            HandleEntryClick(context, entry.path);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsSelected(entry.path))
        {
            // Right-click acts on the item under the mouse; it joins nothing it is not part of.
            SetSelection(context, { entry.path }, entry.path);
            m_SelectionAnchor = entry.path;
        }

        if (ImGui::BeginDragDropSource())
        {
            // Dragging a selected item drags the whole selection (moved by dropping on a folder). Files keep the asset
            // payload so they can also be dropped on inspector fields and the hierarchy.
            m_DragSources = IsSelected(entry.path) && m_Selection.size() > 1 ? GetActionTargets(entry.path)
                : std::vector<fs::path>{ entry.path };
            const std::string assetPath = EditorUtils::ToGameRelativePath(entry.path);
            ImGui::SetDragDropPayload(entry.directory ? EditorUtils::kContentFolderPayload : EditorUtils::kAssetPathPayload,
                assetPath.data(), assetPath.size());
            if (m_DragSources.size() > 1)
            {
                ImGui::Text("%zu items", m_DragSources.size());
            }
            else
            {
                ImGui::TextUnformatted(assetPath.c_str());
            }
            ImGui::EndDragDropSource();
        }
        if (entry.directory)
        {
            DrawMoveDropTarget(entry.path);
        }

        DrawEntryMenu(context, entry.path, entry.directory);

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            m_RenameArmedPath.clear();
            if (entry.directory)
            {
                nextDirectory = entry.path;
            }
            else if (IsSceneFile(entry.path))
            {
                context.RequestOpenScene(entry.path.string());
            }
            else if (IsSourceFile(entry.path))
            {
                EditorUtils::OpenInVisualStudio(entry.path);
            }
        }

        if (!entry.directory && IsSceneFile(entry.path) && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s\nDouble-click to open scene", entry.name.c_str());
        }
        else if (grid && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::SetTooltip("%s", entry.name.c_str());
        }
        ImGui::PopID();
    }

    m_VisibleEntries = std::move(visibleEntries);
    if (!m_RenamingPath.empty() && !renameRowDrawn)
    {
        RenameTrace("row-not-drawn");
    }
    if (!nextDirectory.empty())
    {
        m_CurrentDirectory = nextDirectory;
    }

    if (ImGui::BeginPopupContextWindow("##FileBrowserContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::BeginMenu("Create"))
        {
            if (ImGui::MenuItem("Folder", nullptr, false, CanCreateIn(m_CurrentDirectory)))
            {
                CreateFolder(m_CurrentDirectory);
            }
            if (ImGui::MenuItem("Material"))
            {
                CreateMaterial(context);
            }

            // Classes go through Tools/NewScript.ps1, so they must live under Game/Source.
            const bool inSource = !GetSourceDirectory(m_CurrentDirectory).empty();
            if (ImGui::BeginMenu("C++ Source", inSource))
            {
                if (ImGui::MenuItem("ScriptComponent"))
                {
                    m_NewSourceKind = SourceKind::ScriptComponent;
                    m_OpenNewSource = true;
                }
                if (ImGui::MenuItem("FsmState"))
                {
                    m_NewSourceKind = SourceKind::FsmState;
                    m_OpenNewSource = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Import"))
        {
            if (ImGui::MenuItem("glTF"))
            {
                m_GltfImport.Open(m_CurrentDirectory);
            }
            ImGui::EndMenu();
        }
        if (!m_MoveSources.empty())
        {
            ImGui::Separator();
            DrawMoveHereItem(context, m_CurrentDirectory, "Move Here");
        }
        if (!m_CopySource.empty())
        {
            ImGui::Separator();
            DrawPasteItem(context, m_CurrentDirectory, "Paste");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Show in Explorer"))
        {
            ShellExecuteW(nullptr, L"open", m_CurrentDirectory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (ImGui::MenuItem("Find Missing References"))
        {
            m_Index.Refresh();
            m_Missing = m_Index.FindMissing();
            m_RedirectTargets.assign(m_Missing.size(), std::string());
            m_ShowMissing = true;
        }
        ImGui::EndPopup();
    }

    DrawNewSourcePopup();
    DrawDeleteSourcePopup(context);
}

void FileBrowserPanel::DrawEntryMenu(EditorContext& context, const fs::path& path, bool directory)
{
    if (!ImGui::BeginPopupContextItem())
    {
        return;
    }

    bool drawn = false;
    if (directory)
    {
        drawn = true;
        DrawFolderMenuItems(context, path, false);
    }

    // Part of a multi-selection: the actions that apply to every selected item.
    const std::vector<fs::path> group = GetActionTargets(path);
    if (group.size() > 1)
    {
        if (drawn)
        {
            ImGui::Separator();
        }
        const std::string count = std::to_string(group.size());
        if (ImGui::MenuItem(("Move " + count + " Items").c_str()))
        {
            BeginMove(group);
        }
        if (ImGui::MenuItem(("Delete " + count + " Items").c_str(), "Del"))
        {
            RequestDeletePaths(context, group);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Show in Explorer"))
        {
            EditorUtils::ShowInExplorer(path);
        }
        ImGui::EndPopup();
        return;
    }

    if (CanEdit(path))
    {
        if (drawn)
        {
            ImGui::Separator();
        }
        drawn = true;
        if (ImGui::MenuItem("Rename", directory ? nullptr : "F2"))
        {
            BeginRename(path);
        }
        if (ImGui::MenuItem("Copy", directory ? nullptr : "Ctrl+C"))
        {
            CopyAsset(path);
        }
        if (ImGui::MenuItem("Duplicate", directory ? nullptr : "Ctrl+D"))
        {
            DuplicateAsset(context, path);
        }
        if (ImGui::MenuItem("Move"))
        {
            BeginMove({ path });
        }
        if (ImGui::MenuItem("Find References"))
        {
            ShowReferences(path);
        }
        if (ImGui::MenuItem("Delete", "Del"))
        {
            RequestDelete(context, path);
        }
    }

    if (!directory && IsMeshSourceFile(path))
    {
        drawn = true;
        if (ImGui::MenuItem("Convert to .cnmesh"))
        {
            const std::string source = EditorUtils::ToGameRelativePath(path);
            const std::string target = EditorUtils::ToGameRelativePath(CnmeshPathFor(path));
            m_Message = Canape::Application::Get().GetAssetManager().ExportMesh(source, target)
                ? "Converted " + source + " -> " + target
                : "Failed to convert " + source + " (see log)";
        }
    }

    if (!directory && IsSourceFile(path) && !GetSourceDirectory(path.parent_path()).empty())
    {
        drawn = true;
        if (ImGui::MenuItem("Delete"))
        {
            m_DeleteSource = path.parent_path() / path.stem();
            m_OpenDeleteSource = true;
        }
    }

    if (drawn)
    {
        ImGui::Separator();
    }
    if (ImGui::MenuItem("Show in Explorer"))
    {
        EditorUtils::ShowInExplorer(path);
    }
    ImGui::EndPopup();
}

void FileBrowserPanel::DrawFolderMenuItems(EditorContext& context, const fs::path& folder, bool showRename)
{
    bool drawn = false;
    if (CanCreateIn(folder))
    {
        drawn = true;
        if (ImGui::MenuItem("New Folder"))
        {
            CreateFolder(folder);
        }
    }
    if (!m_MoveSources.empty())
    {
        drawn = true;
        DrawMoveHereItem(context, folder, showRename ? "Move Here" : "Move Into This Folder");
    }
    if (!m_CopySource.empty())
    {
        drawn = true;
        DrawPasteItem(context, folder, showRename ? "Paste" : "Paste Into This Folder");
    }
    if (showRename && CanEdit(folder))
    {
        if (drawn)
        {
            ImGui::Separator();
        }
        drawn = true;
        if (ImGui::MenuItem("Rename"))
        {
            m_CurrentDirectory = folder.parent_path();
            BeginRename(folder);
        }
        if (ImGui::MenuItem("Copy"))
        {
            CopyAsset(folder);
        }
        if (ImGui::MenuItem("Duplicate"))
        {
            DuplicateAsset(context, folder);
        }
        if (ImGui::MenuItem("Move"))
        {
            BeginMove({ folder });
        }
        if (ImGui::MenuItem("Find References"))
        {
            ShowReferences(folder);
        }
        if (ImGui::MenuItem("Delete"))
        {
            RequestDelete(context, folder);
        }
    }
    if (showRename)
    {
        if (drawn)
        {
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Show in Explorer"))
        {
            EditorUtils::ShowInExplorer(folder);
        }
    }
}

void FileBrowserPanel::DrawMoveHereItem(EditorContext& context, const fs::path& folder, const char* label)
{
    // Enabled when at least one item can go there; the others are skipped (and listed before anything moves).
    std::string firstError;
    size_t invalid = 0;
    for (const fs::path& source : m_MoveSources)
    {
        const std::string error = AssetRefactor::ValidateMove(m_Index.GetRoot(), ToAssetPath(source), ToAssetPath(folder));
        if (!error.empty())
        {
            ++invalid;
            if (firstError.empty())
            {
                firstError = error;
            }
        }
    }
    const bool enabled = invalid < m_MoveSources.size();
    if (ImGui::MenuItem(label, nullptr, false, enabled))
    {
        MoveTo(context, m_MoveSources, folder);
    }
    if (invalid > 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        if (m_MoveSources.size() == 1)
        {
            ImGui::SetTooltip("%s", firstError.c_str());
        }
        else
        {
            ImGui::SetTooltip("%zu of %zu items cannot be moved here: %s", invalid, m_MoveSources.size(), firstError.c_str());
        }
    }
    if (ImGui::MenuItem("Cancel Move"))
    {
        m_MoveSources.clear();
    }
}

void FileBrowserPanel::DrawMoveDropTarget(const fs::path& folder)
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || m_DragSources.empty() || !CanCreateIn(folder)
        || !(payload->IsDataType(EditorUtils::kAssetPathPayload) || payload->IsDataType(EditorUtils::kContentFolderPayload)))
    {
        return;
    }
    // A folder that is itself being dragged is not a target; otherwise only folders that can take at least one of the
    // dragged items light up.
    if (std::find(m_DragSources.begin(), m_DragSources.end(), folder) != m_DragSources.end())
    {
        return;
    }
    const bool any = std::any_of(m_DragSources.begin(), m_DragSources.end(), [&](const fs::path& source)
    {
        return AssetRefactor::ValidateMove(m_Index.GetRoot(), ToAssetPath(source), ToAssetPath(folder)).empty();
    });
    if (!any || !ImGui::BeginDragDropTarget())
    {
        return;
    }
    if (ImGui::AcceptDragDropPayload(payload->DataType))
    {
        m_DropMoveSources = m_DragSources;
        m_DropMoveTarget = folder;
    }
    ImGui::EndDragDropTarget();
}

void FileBrowserPanel::DrawNewSourcePopup()
{
    constexpr const char* kPopup = "New C++ Source";
    if (m_OpenNewSource)
    {
        ImGui::OpenPopup(kPopup);
        m_NewSourceName.clear();
        m_OpenNewSource = false;
    }
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    const std::string directory = GetSourceDirectory(m_CurrentDirectory);
    const char* kind = m_NewSourceKind == SourceKind::ScriptComponent ? "ScriptComponent" : "FsmState";
    ImGui::Text("%s in Game/%s", kind, directory.c_str());

    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    const bool submitted = ImGui::InputText("Class", &m_NewSourceName, ImGuiInputTextFlags_EnterReturnsTrue);

    if (ImGui::Button("Create") || submitted)
    {
        std::wstring arguments = L"-Name " + EditorUtils::FromUtf8(m_NewSourceName).wstring()
            + L" -Directory " + EditorUtils::FromUtf8(directory).wstring();
        if (m_NewSourceKind == SourceKind::FsmState)
        {
            arguments += L" -State";
        }

        std::string error;
        m_Message = EditorUtils::RunTool("NewScript.ps1", arguments, error)
            ? "Created " + m_NewSourceName + ".h/.cpp. Rebuild the editor to use it."
            : error;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FileBrowserPanel::DrawDeleteSourcePopup(EditorContext& context)
{
    constexpr const char* kPopup = "Delete C++ Source";
    if (m_OpenDeleteSource)
    {
        ImGui::OpenPopup(kPopup);
        m_OpenDeleteSource = false;
    }
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    std::vector<fs::path> files;
    std::error_code error;
    for (const char* extension : { ".h", ".cpp" })
    {
        fs::path file = m_DeleteSource;
        file += extension;
        if (fs::exists(file, error))
        {
            files.push_back(file);
        }
    }

    ImGui::TextUnformatted("Move to the Recycle Bin and remove from Game.vcxproj:");
    for (const fs::path& file : files)
    {
        ImGui::BulletText("%s", EditorUtils::ToGameRelativePath(file).c_str());
    }

    if (ImGui::Button("Delete"))
    {
        std::string message;
        const std::string path = EditorUtils::ToGameRelativePath(m_DeleteSource);
        if (EditorUtils::RunTool("RemoveSource.ps1", EditorUtils::FromUtf8(path).wstring(), message))
        {
            m_Message = "Deleted " + path + ". Rebuild the editor.";
            if (std::find(files.begin(), files.end(), context.GetSelectedAsset()) != files.end())
            {
                context.SelectAsset({});
            }
        }
        else
        {
            m_Message = message;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FileBrowserPanel::CreateMaterial(EditorContext& context)
{
    std::error_code error;
    fs::path file = m_CurrentDirectory / (std::string("NewMaterial") + Canape::MaterialFile::kExtension);
    for (int index = 1; fs::exists(file, error); ++index)
    {
        file = m_CurrentDirectory / ("NewMaterial" + std::to_string(index) + Canape::MaterialFile::kExtension);
    }

    if (Canape::MaterialFile::Save(file, Canape::MaterialData{}))
    {
        context.SelectAsset(file);
    }
}

void FileBrowserPanel::MigrateLegacyWindowSettings()
{
    if (m_SettingsMigrated)
    {
        return;
    }
    m_SettingsMigrated = true;

    const ImGuiWindowSettings* legacy = ImGui::FindWindowSettingsByID(ImHashStr(kLegacyName));
    if (!legacy || ImGui::FindWindowSettingsByID(ImHashStr(kName)))
    {
        return;
    }
    const ImGuiWindowSettings copy = *legacy;
    ImGuiWindowSettings* settings = ImGui::CreateNewWindowSettings(kName);
    settings->Pos = copy.Pos;
    settings->Size = copy.Size;
    settings->ViewportPos = copy.ViewportPos;
    settings->ViewportId = copy.ViewportId;
    settings->DockId = copy.DockId;
    settings->ClassId = copy.ClassId;
    settings->DockOrder = copy.DockOrder;
    settings->Collapsed = copy.Collapsed;
    ImGui::MarkIniSettingsDirty();
}

void FileBrowserPanel::QueueExternalDrop(std::vector<fs::path> files)
{
    m_DroppedFiles.insert(m_DroppedFiles.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
}

void FileBrowserPanel::ProcessExternalDrops()
{
    if (!m_DroppedFiles.empty())
    {
        size_t ignored = 0;
        for (const fs::path& file : m_DroppedFiles)
        {
            const std::string extension = AssetPaths::Normalize(EditorUtils::ToUtf8(file.extension()));
            if (extension == ".gltf")
            {
                m_PendingImports.push_back(file);
            }
            else
            {
                ++ignored;
            }
        }
        m_DroppedFiles.clear();
        m_Message = ignored == 0 ? std::string()
            : std::to_string(ignored) + " dropped file(s) ignored. Only .gltf files can be imported by dropping.";
        ImGui::SetWindowFocus(kName);
    }

    if (!m_PendingImports.empty() && !m_GltfImport.IsOpen())
    {
        const fs::path target = CanCreateIn(m_CurrentDirectory) ? m_CurrentDirectory : EditorUtils::GetGameDirectory() / "Content";
        m_GltfImport.Open(target, m_PendingImports.front());
        m_PendingImports.erase(m_PendingImports.begin());
    }
}

void FileBrowserPanel::EnsureIndex()
{
    if (m_Index.IsBuilt())
    {
        return;
    }
    m_Index.SetRoot(EditorUtils::GetGameDirectory());
    const auto start = std::chrono::steady_clock::now();
    m_Index.Rebuild();
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    CN_LOG_GAME("Editor", "Asset reference index built: ", m_Index.GetFileCount(), " files in ", elapsed, " ms");
}

std::string FileBrowserPanel::ToAssetPath(const fs::path& path) const
{
    return AssetPaths::ToGameRelative(EditorUtils::GetGameDirectory(), path);
}

bool FileBrowserPanel::CanEdit(const fs::path& path) const
{
    return !path.empty() && AssetRefactor::IsUnderContent(ToAssetPath(path));
}

bool FileBrowserPanel::CanCreateIn(const fs::path& folder) const
{
    const std::string relative = ToAssetPath(folder);
    return AssetPaths::Normalize(relative) == "content" || AssetRefactor::IsUnderContent(relative);
}

void FileBrowserPanel::CreateFolder(const fs::path& parent)
{
    std::error_code error;
    fs::path folder = parent / "New Folder";
    for (int index = 1; fs::exists(folder, error); ++index)
    {
        folder = parent / ("New Folder " + std::to_string(index));
    }
    if (!fs::create_directory(folder, error))
    {
        m_Message = "Failed to create a folder in " + ToAssetPath(parent);
        return;
    }
    m_CurrentDirectory = parent;
    BeginRename(folder);
}

void FileBrowserPanel::RenameTrace(const char* event) const
{
    static std::FILE* file = []
    {
        std::FILE* f = nullptr;
        const fs::path path = (EditorUtils::GetGameDirectory() / ".." / "Intermediate" / "rename_trace.log").lexically_normal();
        _wfopen_s(&f, path.c_str(), L"w");
        return f;
    }();
    if (!file)
    {
        return;
    }
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    std::fprintf(file, "frame=%d event=%s renaming=%s item=%08X active=%08X activePrev=%08X nav=%s focusedWin=%s wantText=%d appFocused=%d keyEnter=%d keyEsc=%d mouse0=%d\n",
        ImGui::GetFrameCount(), event, EditorUtils::ToUtf8(m_RenamingPath.filename()).c_str(), ImGui::GetItemID(), g.ActiveId,
        g.ActiveIdPreviousFrame, g.NavWindow ? g.NavWindow->Name : "-", g.NavWindow ? g.NavWindow->RootWindow->Name : "-",
        ImGui::GetIO().WantTextInput ? 1 : 0, ImGui::GetIO().AppFocusLost ? 0 : 1, ImGui::IsKeyDown(ImGuiKey_Enter) ? 1 : 0,
        ImGui::IsKeyDown(ImGuiKey_Escape) ? 1 : 0, ImGui::IsMouseDown(ImGuiMouseButton_Left) ? 1 : 0);
    std::fflush(file);
}

void FileBrowserPanel::BeginRename(const fs::path& path)
{
    RenameTrace("BeginRename");
    std::error_code error;
    m_RenamingPath = path;
    m_RenameBuffer = EditorUtils::ToUtf8(fs::is_directory(path, error) ? path.filename() : path.stem());
    m_RenameFocus = true;
    m_RenameArmedPath.clear();
}

void FileBrowserPanel::CancelRename()
{
    m_RenamingPath.clear();
    m_RenameBuffer.clear();
}

void FileBrowserPanel::CommitRename(EditorContext& context)
{
    const fs::path path = m_RenamingPath;
    std::string name = m_RenameBuffer;
    CancelRename();

    std::error_code error;
    const bool directory = fs::is_directory(path, error);
    if (!directory)
    {
        name += EditorUtils::ToUtf8(path.extension());
    }
    if (name == EditorUtils::ToUtf8(path.filename()))
    {
        return;
    }

    const bool emptyFolder = directory && fs::is_empty(path, error);
    const std::string assetPath = ToAssetPath(path);
    RequestPlan(context, [this, assetPath, name] { return AssetRefactor::PlanRename(m_Index, assetPath, name); }, !emptyFolder);
}

void FileBrowserPanel::BeginMove(std::vector<fs::path> paths)
{
    paths.erase(std::remove_if(paths.begin(), paths.end(), [this](const fs::path& path) { return !CanEdit(path); }), paths.end());
    m_MoveSources = std::move(paths);
    m_Message.clear();
}

void FileBrowserPanel::MoveTo(EditorContext& context, std::vector<fs::path> sources, const fs::path& folder)
{
    if (sources.empty())
    {
        return;
    }
    if (sources.size() > 1)
    {
        RequestBatch(context, false, sources, folder);
        return;
    }
    const fs::path& source = sources.front();
    std::error_code error;
    const bool emptyFolder = fs::is_directory(source, error) && fs::is_empty(source, error);
    const std::string assetPath = ToAssetPath(source);
    const std::string target = ToAssetPath(folder);
    if (const std::string reason = AssetRefactor::ValidateMove(m_Index.GetRoot(), assetPath, target); !reason.empty())
    {
        m_Message = "Cannot move " + assetPath + ": " + reason;
        return;
    }
    RequestPlan(context, [this, assetPath, target] { return AssetRefactor::PlanMove(m_Index, assetPath, target); }, !emptyFolder);
}

void FileBrowserPanel::ShowReferences(const fs::path& path)
{
    m_Index.Refresh();
    std::error_code error;
    m_ReferencesTarget = ToAssetPath(path);
    m_References = fs::is_directory(path, error)
        ? m_Index.FindReferencingFilesUnder(m_ReferencesTarget)
        : m_Index.FindReferencingFiles(m_ReferencesTarget);
    m_ShowReferences = true;
}

void FileBrowserPanel::RequestPlan(EditorContext& context, const std::function<RefactorPlan()>& build, bool needsScene)
{
    if (needsScene && m_Callbacks.prepareScene && !m_Callbacks.prepareScene())
    {
        m_Message = "Cancelled.";
        return;
    }

    RefactorPlan plan = build();
    if (!plan.IsValid())
    {
        m_Message = plan.error;
        return;
    }
    if (!plan.DeletesOnDisk() && plan.edits.empty() && plan.warnings.empty() && plan.startupSceneBefore.empty())
    {
        ApplyPlan(context, plan, needsScene);
        return;
    }
    m_PendingPlan = std::move(plan);
    m_PendingSceneChecked = needsScene;
    m_OpenPlanPopup = true;
}

bool FileBrowserPanel::ApplyPlan(EditorContext& context, const RefactorPlan& plan, bool sceneChecked)
{
    std::string error;
    const bool applied = AssetRefactor::Apply(plan, m_Index, error);

    const fs::path& root = m_Index.GetRoot();
    std::error_code existsError;
    const bool caseOnly = AssetPaths::Normalize(plan.move.from) == AssetPaths::Normalize(plan.move.to);
    const bool movedOnDisk = plan.MovesOnDisk()
        && fs::exists(AssetPaths::ToAbsolute(root, plan.move.to), existsError)
        && (caseOnly || !fs::exists(AssetPaths::ToAbsolute(root, plan.move.from), existsError));

    const auto remap = [&](const fs::path& path)
    {
        if (path.empty() || !movedOnDisk)
        {
            return path;
        }
        const std::string relative = ToAssetPath(path);
        const std::string moved = AssetRefactor::RemapGameRelative(relative, plan);
        return moved == relative ? path : AssetPaths::ToAbsolute(root, moved);
    };
    const bool deletedOnDisk = plan.DeletesOnDisk() && !fs::exists(AssetPaths::ToAbsolute(root, plan.move.from), existsError);
    const auto isDeleted = [&](const fs::path& path)
    {
        return deletedOnDisk && !path.empty() && AssetRefactor::Remap(ToAssetPath(path), plan.move).has_value();
    };

    if (!context.GetSelectedAsset().empty())
    {
        context.SelectAsset(isDeleted(context.GetSelectedAsset()) ? fs::path() : remap(context.GetSelectedAsset()));
    }
    m_CurrentDirectory = isDeleted(m_CurrentDirectory)
        ? AssetPaths::ToAbsolute(root, plan.move.from).parent_path()
        : remap(m_CurrentDirectory);
    m_MoveSources.clear();

    if (plan.DeletesOnDisk())
    {
        m_Message = "Deleted " + plan.move.from + " (moved to the Recycle Bin). Cleared "
            + std::to_string(plan.edits.size()) + " references in " + std::to_string(plan.files.size()) + " files.";
    }
    else
    {
        const char* verb = plan.kind == RefactorPlan::Kind::Rename ? "Renamed"
            : plan.kind == RefactorPlan::Kind::Move ? "Moved" : "Redirected";
        m_Message = std::string(verb) + " " + plan.move.from + " -> " + plan.move.to + ". Updated "
            + std::to_string(plan.edits.size()) + " references in " + std::to_string(plan.files.size()) + " files.";
    }
    if (!plan.warnings.empty())
    {
        m_Message += " " + std::to_string(plan.warnings.size()) + " item(s) need manual updates (see log).";
    }
    if (!applied)
    {
        m_Message = "Failed: " + error;
    }

    CN_LOG_GAME("Editor", m_Message);
    for (const std::string& warning : plan.warnings)
    {
        CN_LOG_GAME("Editor", "Manual update needed: ", warning);
    }

    if (m_Callbacks.afterApply)
    {
        m_Callbacks.afterApply(plan, movedOnDisk || deletedOnDisk, sceneChecked);
    }
    if (m_ShowMissing)
    {
        m_Missing = m_Index.FindMissing();
        m_RedirectTargets.assign(m_Missing.size(), std::string());
    }
    if (m_ShowReferences)
    {
        m_ShowReferences = false;
    }
    return applied;
}

std::string FileBrowserPanel::OpenSceneDeleteError(EditorContext& context, const std::string& assetPath) const
{
    const std::string scenePath = context.GetScenePath().empty() ? std::string() : ToAssetPath(fs::path(context.GetScenePath()));
    std::error_code error;
    const PathMove deleted{ assetPath, std::string(), fs::is_directory(AssetPaths::ToAbsolute(m_Index.GetRoot(), assetPath), error) };
    if (!scenePath.empty() && AssetRefactor::Remap(scenePath, deleted))
    {
        return "Cannot delete the scene that is open in the editor. Open another scene first.";
    }
    return {};
}

void FileBrowserPanel::RequestDelete(EditorContext& context, const fs::path& path)
{
    const std::string assetPath = ToAssetPath(path);
    if (std::string reason = OpenSceneDeleteError(context, assetPath); !reason.empty())
    {
        m_Message = std::move(reason);
        return;
    }

    std::error_code error;
    const bool emptyFolder = fs::is_directory(path, error) && fs::is_empty(path, error);
    RequestPlan(context, [this, assetPath] { return AssetRefactor::PlanDelete(m_Index, assetPath); }, !emptyFolder);
}

void FileBrowserPanel::RequestDeletePaths(EditorContext& context, const std::vector<fs::path>& paths)
{
    if (paths.size() == 1)
    {
        RequestDelete(context, paths.front());
    }
    else if (!paths.empty())
    {
        RequestBatch(context, true, paths, {});
    }
}

void FileBrowserPanel::RequestBatch(EditorContext& context, bool deleting, const std::vector<fs::path>& paths, const fs::path& folder)
{
    std::error_code error;
    const bool needsScene = std::any_of(paths.begin(), paths.end(),
        [&](const fs::path& path) { return !(fs::is_directory(path, error) && fs::is_empty(path, error)); });
    if (needsScene && m_Callbacks.prepareScene && !m_Callbacks.prepareScene())
    {
        m_Message = "Cancelled.";
        return;
    }

    BatchRequest request;
    request.deleting = deleting;
    request.targetFolder = deleting ? std::string() : ToAssetPath(folder);
    request.sceneChecked = needsScene;
    size_t valid = 0;
    bool needsConfirm = deleting;
    for (const fs::path& path : paths)
    {
        const std::string assetPath = ToAssetPath(path);
        RefactorPlan plan;
        const std::string sceneError = deleting ? OpenSceneDeleteError(context, assetPath) : std::string();
        if (!sceneError.empty())
        {
            plan.kind = RefactorPlan::Kind::Delete;
            plan.move = { assetPath, std::string(), fs::is_directory(path, error) };
            plan.error = sceneError;
        }
        else
        {
            plan = deleting ? AssetRefactor::PlanDelete(m_Index, assetPath) : AssetRefactor::PlanMove(m_Index, assetPath, request.targetFolder);
        }
        if (plan.IsValid())
        {
            ++valid;
            needsConfirm = needsConfirm || !plan.edits.empty() || !plan.warnings.empty() || !plan.startupSceneBefore.empty();
        }
        else
        {
            // Skipped items are listed in the confirmation, so the user sees why they stay.
            needsConfirm = true;
            if (plan.move.from.empty())
            {
                plan.move.from = assetPath;
            }
        }
        request.assets.push_back(assetPath);
        request.preview.push_back(std::move(plan));
    }

    if (valid == 0)
    {
        m_Message = "Cannot " + std::string(deleting ? "delete" : "move") + " the selected items: " + request.preview.front().error;
        return;
    }
    if (!needsConfirm)
    {
        ApplyBatch(context, request);
        return;
    }
    m_PendingBatch = std::move(request);
    m_OpenBatchPopup = true;
}

void FileBrowserPanel::ApplyBatch(EditorContext& context, const BatchRequest& request)
{
    // Each item is planned again just before it is applied: the earlier items may have rewritten its references.
    size_t applied = 0;
    size_t edits = 0;
    size_t warnings = 0;
    std::vector<std::string> failures;
    for (const std::string& assetPath : request.assets)
    {
        if (request.deleting)
        {
            if (std::string reason = OpenSceneDeleteError(context, assetPath); !reason.empty())
            {
                failures.push_back(assetPath + ": " + reason);
                continue;
            }
        }
        const RefactorPlan plan = request.deleting
            ? AssetRefactor::PlanDelete(m_Index, assetPath)
            : AssetRefactor::PlanMove(m_Index, assetPath, request.targetFolder);
        if (!plan.IsValid())
        {
            failures.push_back(assetPath + ": " + plan.error);
            continue;
        }
        if (ApplyPlan(context, plan, request.sceneChecked))
        {
            ++applied;
            edits += plan.edits.size();
            warnings += plan.warnings.size();
        }
        else
        {
            failures.push_back(assetPath + ": " + m_Message);
        }
    }
    m_MoveSources.clear();

    if (request.deleting)
    {
        m_Message = "Deleted " + std::to_string(applied) + " items (moved to the Recycle Bin). Cleared "
            + std::to_string(edits) + " references.";
    }
    else
    {
        m_Message = "Moved " + std::to_string(applied) + " items to " + request.targetFolder + ". Updated "
            + std::to_string(edits) + " references.";
    }
    if (warnings > 0)
    {
        m_Message += " " + std::to_string(warnings) + " item(s) need manual updates (see log).";
    }
    if (!failures.empty())
    {
        m_Message += " " + std::to_string(failures.size()) + " skipped (see log).";
    }
    CN_LOG_GAME("Editor", m_Message);
    for (const std::string& failure : failures)
    {
        CN_LOG_GAME("Editor", "Skipped ", failure);
    }
}

void FileBrowserPanel::DrawBatchPopup(EditorContext& context)
{
    constexpr const char* kPopup = "Move / Delete Items";
    if (m_OpenBatchPopup)
    {
        ImGui::OpenPopup(kPopup);
        m_OpenBatchPopup = false;
    }
    ImGui::SetNextWindowSize(ImVec2(760.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopup))
    {
        return;
    }
    if (!m_PendingBatch)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const BatchRequest& request = *m_PendingBatch;
    const bool deleting = request.deleting;
    size_t valid = 0;
    size_t edits = 0;
    size_t files = 0;
    size_t warnings = 0;
    for (const RefactorPlan& plan : request.preview)
    {
        if (plan.IsValid())
        {
            ++valid;
            edits += plan.edits.size();
            files += plan.files.size();
            warnings += plan.warnings.size();
        }
    }

    if (deleting)
    {
        ImGui::Text("Delete %zu items?", valid);
        ImGui::TextDisabled("They will be moved to the Recycle Bin.");
    }
    else
    {
        ImGui::Text("Move %zu items", valid);
        ImGui::Text("  ->  %s", request.targetFolder.c_str());
    }
    ImGui::Separator();

    if (ImGui::BeginChild("##BatchItems", ImVec2(0.0f, 260.0f), ImGuiChildFlags_Borders))
    {
        for (size_t i = 0; i < request.preview.size(); ++i)
        {
            const RefactorPlan& plan = request.preview[i];
            ImGui::PushID(static_cast<int>(i));
            if (!plan.IsValid())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Skipped  %s", request.assets[i].c_str());
                ImGui::Indent();
                ImGui::TextWrapped("%s", plan.error.c_str());
                ImGui::Unindent();
                ImGui::PopID();
                continue;
            }

            const std::string title = request.assets[i] + (plan.move.directory ? "/" : "");
            const bool hasDetails = !plan.edits.empty() || !plan.companions.empty() || !plan.warnings.empty()
                || !plan.startupSceneBefore.empty();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasDetails)
            {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            const bool open = ImGui::TreeNodeEx("##Item", flags, "%s", title.c_str());
            if (!plan.edits.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%zu references in %zu files %s", plan.edits.size(),
                    plan.files.size(), deleting ? "cleared" : "updated");
            }
            if (open && hasDetails)
            {
                for (const PathMove& companion : plan.companions)
                {
                    ImGui::BulletText(deleting ? "Also deletes %s" : "Also moves %s", companion.from.c_str());
                }
                for (const ReferenceEdit& edit : plan.edits)
                {
                    ImGui::BulletText("%s: %s  ->  %s", edit.file.c_str(), edit.before.c_str(),
                        edit.after.empty() ? "(cleared)" : edit.after.c_str());
                }
                if (!plan.startupSceneBefore.empty())
                {
                    ImGui::BulletText("%s startupScene: %s  ->  %s", std::string(AssetRefactor::kGameConfig).c_str(),
                        plan.startupSceneBefore.c_str(), plan.startupSceneAfter.c_str());
                }
                for (const std::string& warning : plan.warnings)
                {
                    ImGui::BulletText("Not updated automatically: %s", warning.c_str());
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (edits > 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%zu references in %zu files will be %s.", edits, files,
            deleting ? "cleared" : "updated");
    }
    if (warnings > 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%zu item(s) need manual updates (expand the items above).", warnings);
    }
    if (valid < request.preview.size())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%zu item(s) will be skipped.", request.preview.size() - valid);
    }

    ImGui::Spacing();
    ImGui::TextDisabled("File changes cannot be undone from the editor. Commit or stash your work first.");
    ImGui::BeginDisabled(valid == 0);
    const bool confirm = ImGui::Button(deleting ? "Delete" : "Move");
    ImGui::EndDisabled();
    if (confirm)
    {
        const BatchRequest applied = std::move(*m_PendingBatch);
        m_PendingBatch.reset();
        ImGui::CloseCurrentPopup();
        ApplyBatch(context, applied);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        m_PendingBatch.reset();
        m_Message = "Cancelled.";
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

std::string FileBrowserPanel::RunDeleteCommand(EditorContext& context, const std::string& assetPath)
{
    EnsureIndex();
    if (std::string reason = OpenSceneDeleteError(context, assetPath); !reason.empty())
    {
        m_Message = std::move(reason);
    }
    else if (m_Callbacks.prepareScene && !m_Callbacks.prepareScene())
    {
        m_Message = "Cancelled.";
    }
    else
    {
        const RefactorPlan plan = AssetRefactor::PlanDelete(m_Index, assetPath);
        if (plan.IsValid())
        {
            ApplyPlan(context, plan, true);
            return m_Message;
        }
        m_Message = plan.error;
    }
    CN_LOG_GAME("Editor", "Cannot delete ", assetPath, ": ", m_Message);
    return m_Message;
}

std::string FileBrowserPanel::RunRefactorCommand(EditorContext& context, bool move, const std::string& assetPath, const std::string& argument)
{
    EnsureIndex();
    if (m_Callbacks.prepareScene && !m_Callbacks.prepareScene())
    {
        m_Message = "Cancelled.";
        CN_LOG_GAME("Editor", m_Message);
        return m_Message;
    }
    const RefactorPlan plan = move
        ? AssetRefactor::PlanMove(m_Index, assetPath, argument)
        : AssetRefactor::PlanRename(m_Index, assetPath, argument);
    if (!plan.IsValid())
    {
        m_Message = plan.error;
        CN_LOG_GAME("Editor", "Cannot ", move ? "move " : "rename ", assetPath, ": ", plan.error);
        return m_Message;
    }
    ApplyPlan(context, plan, true);
    return m_Message;
}

void FileBrowserPanel::DrawPlanPopup(EditorContext& context)
{
    constexpr const char* kPopup = "Update Asset References";
    if (m_OpenPlanPopup)
    {
        ImGui::OpenPopup(kPopup);
        m_OpenPlanPopup = false;
    }
    ImGui::SetNextWindowSize(ImVec2(760.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopup))
    {
        return;
    }
    if (!m_PendingPlan)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const RefactorPlan& plan = *m_PendingPlan;
    const bool deleting = plan.DeletesOnDisk();
    if (deleting)
    {
        ImGui::Text("Delete %s%s?", plan.move.from.c_str(), plan.move.directory ? " and everything inside it" : "");
        ImGui::TextDisabled("It will be moved to the Recycle Bin.");
    }
    else
    {
        const char* verb = plan.kind == RefactorPlan::Kind::Rename ? "Rename"
            : plan.kind == RefactorPlan::Kind::Move ? "Move" : "Redirect references";
        ImGui::Text("%s  %s", verb, plan.move.from.c_str());
        ImGui::Text("  ->  %s", plan.move.to.c_str());
    }
    for (const PathMove& companion : plan.companions)
    {
        ImGui::BulletText(deleting ? "Also deletes %s" : "Also moves %s", companion.from.c_str());
    }
    ImGui::Separator();

    if (!plan.edits.empty() && deleting)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "This is used in %zu places in %zu files. These references will be cleared:",
            plan.edits.size(), plan.files.size());
    }
    if (!plan.edits.empty())
    {
        if (!deleting)
        {
            ImGui::Text("%zu references in %zu files will be updated:", plan.edits.size(), plan.files.size());
        }
        if (ImGui::BeginChild("##Edits", ImVec2(0.0f, 240.0f), ImGuiChildFlags_Borders))
        {
            for (const std::string& file : plan.files)
            {
                if (ImGui::TreeNodeEx(file.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    for (const ReferenceEdit& edit : plan.edits)
                    {
                        if (edit.file == file)
                        {
                            ImGui::BulletText("%s  ->  %s", edit.before.c_str(), edit.after.empty() ? "(cleared)" : edit.after.c_str());
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }
        ImGui::EndChild();
    }
    else
    {
        ImGui::TextUnformatted(deleting ? "Nothing references it." : "No asset files reference this path.");
    }

    if (!plan.startupSceneBefore.empty())
    {
        ImGui::BulletText("%s startupScene: %s  ->  %s", std::string(AssetRefactor::kGameConfig).c_str(),
            plan.startupSceneBefore.c_str(), plan.startupSceneAfter.c_str());
    }
    if (!plan.warnings.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Not updated automatically:");
        for (const std::string& warning : plan.warnings)
        {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", warning.c_str());
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("File changes cannot be undone from the editor. Commit or stash your work first.");
    if (ImGui::Button(deleting ? "Delete" : "Apply"))
    {
        const RefactorPlan applied = std::move(*m_PendingPlan);
        m_PendingPlan.reset();
        ImGui::CloseCurrentPopup();
        ApplyPlan(context, applied, m_PendingSceneChecked);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        m_PendingPlan.reset();
        m_Message = "Cancelled.";
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FileBrowserPanel::DrawReferencesWindow(EditorContext& context)
{
    if (!m_ShowReferences)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 320.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Asset References", &m_ShowReferences))
    {
        ImGui::TextUnformatted(m_ReferencesTarget.c_str());
        ImGui::Text("Referenced by %zu files", m_References.size());
        ImGui::Separator();
        if (m_References.empty())
        {
            ImGui::TextDisabled("No references.");
        }
        for (const std::string& file : m_References)
        {
            if (ImGui::Selectable(file.c_str()))
            {
                const fs::path absolute = AssetPaths::ToAbsolute(m_Index.GetRoot(), file);
                m_CurrentDirectory = absolute.parent_path();
                context.SelectAsset(absolute);
            }
        }
    }
    ImGui::End();
}

void FileBrowserPanel::DrawMissingWindow(EditorContext& context)
{
    if (!m_ShowMissing)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(900.0f, 360.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Missing References", &m_ShowMissing))
    {
        if (ImGui::Button("Refresh"))
        {
            m_Index.Refresh();
            m_Missing = m_Index.FindMissing();
            m_RedirectTargets.assign(m_Missing.size(), std::string());
        }
        ImGui::SameLine();
        ImGui::Text("%zu missing paths. Drop an asset or type a path, then Redirect.", m_Missing.size());

        size_t redirectIndex = m_Missing.size();
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##Missing", 3, flags))
        {
            ImGui::TableSetupColumn("Missing Path", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableSetupColumn("Used By", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Redirect To", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < m_Missing.size() && i < m_RedirectTargets.size(); ++i)
            {
                const ReferenceIndex::MissingReference& missing = m_Missing[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(missing.assetPath.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%zu files", missing.files.size());
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    for (const std::string& file : missing.files)
                    {
                        ImGui::TextUnformatted(file.c_str());
                    }
                    ImGui::EndTooltip();
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-90.0f);
                ImGui::InputText("##Target", &m_RedirectTargets[i]);
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorUtils::kAssetPathPayload))
                    {
                        m_RedirectTargets[i].assign(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(m_RedirectTargets[i].empty());
                if (ImGui::Button("Redirect"))
                {
                    redirectIndex = i;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (redirectIndex < m_Missing.size())
        {
            const std::string from = m_Missing[redirectIndex].assetPath;
            const std::string to = m_RedirectTargets[redirectIndex];
            RequestPlan(context, [this, from, to] { return AssetRefactor::PlanRedirect(m_Index, from, to); }, true);
        }
    }
    ImGui::End();
}

namespace
{
    // "Name.ext" -> "Name (1).ext", "Name (1).ext" -> "Name (2).ext", until the name is free in `folder`.
    fs::path MakeUniqueCopyPath(const fs::path& folder, const fs::path& source, bool directory)
    {
        std::error_code error;
        fs::path target = folder / source.filename();
        if (!fs::exists(target, error))
        {
            return target;
        }

        std::string stem = EditorUtils::ToUtf8(directory ? source.filename() : source.stem());
        const std::string extension = directory ? std::string() : EditorUtils::ToUtf8(source.extension());
        if (const size_t open = stem.rfind(" ("); open != std::string::npos && stem.back() == ')' && open + 2 < stem.size() - 1
            && std::all_of(stem.begin() + open + 2, stem.end() - 1, [](char c) { return c >= '0' && c <= '9'; }))
        {
            stem.erase(open);
        }
        for (int index = 1;; ++index)
        {
            target = folder / EditorUtils::FromUtf8(stem + " (" + std::to_string(index) + ")" + extension);
            if (!fs::exists(target, error))
            {
                return target;
            }
        }
    }

    // Buffer / image files a .gltf references by a plain sibling file name (the same rule moves and deletes use).
    std::vector<std::string> GltfSiblingFiles(const fs::path& gltfPath)
    {
        std::vector<std::string> files;
        std::string text;
        if (!AssetPaths::ReadText(gltfPath, text))
        {
            return files;
        }
        const nlohmann::json gltf = nlohmann::json::parse(text, nullptr, false);
        if (gltf.is_discarded() || !gltf.is_object())
        {
            return files;
        }
        for (const char* section : { "buffers", "images" })
        {
            const auto found = gltf.find(section);
            if (found == gltf.end() || !found->is_array())
            {
                continue;
            }
            for (const nlohmann::json& item : *found)
            {
                const std::string uri = item.is_object() ? item.value("uri", std::string()) : std::string();
                if (!uri.empty() && !uri.starts_with("data:") && uri.find_first_of("/\\:") == std::string::npos)
                {
                    files.push_back(uri);
                }
            }
        }
        return files;
    }
}

void FileBrowserPanel::CopyAsset(const fs::path& path)
{
    if (!CanEdit(path))
    {
        return;
    }
    m_CopySource = path;
    m_Message = "Copied " + ToAssetPath(path) + ". Ctrl+V or right-click > Paste to place it.";
}

std::string FileBrowserPanel::ValidatePaste(const fs::path& folder) const
{
    std::error_code error;
    if (m_CopySource.empty() || !fs::exists(m_CopySource, error))
    {
        return "The copied item no longer exists.";
    }
    if (!CanCreateIn(folder))
    {
        return "Paste is only allowed under Content.";
    }
    if (fs::is_directory(m_CopySource, error))
    {
        const fs::path source = m_CopySource.lexically_normal();
        const fs::path target = folder.lexically_normal();
        const fs::path relative = target.lexically_relative(source);
        if (!relative.empty() && *relative.begin() != "..")
        {
            return "A folder cannot be pasted into itself.";
        }
    }
    return {};
}

void FileBrowserPanel::DrawPasteItem(EditorContext& context, const fs::path& folder, const char* label)
{
    const std::string error = ValidatePaste(folder);
    if (ImGui::MenuItem(label, "Ctrl+V", false, error.empty()))
    {
        PasteInto(context, folder);
    }
    if (!error.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", error.c_str());
    }
}

void FileBrowserPanel::PasteInto(EditorContext& context, const fs::path& folder)
{
    if (const std::string reason = ValidatePaste(folder); !reason.empty())
    {
        m_Message = "Cannot paste: " + reason;
        return;
    }

    std::error_code error;
    const bool directory = fs::is_directory(m_CopySource, error);
    const fs::path target = MakeUniqueCopyPath(folder, m_CopySource, directory);
    if (directory)
    {
        fs::copy(m_CopySource, target, fs::copy_options::recursive, error);
    }
    else
    {
        fs::copy_file(m_CopySource, target, fs::copy_options::none, error);
    }
    if (error)
    {
        m_Message = "Failed to copy " + ToAssetPath(m_CopySource) + ": " + error.message();
        return;
    }

    // A .gltf pasted into another folder needs its .bin / textures next to it; existing files there are kept.
    std::string note;
    if (!directory && EditorUtils::ToUtf8(target.extension()) == ".gltf" && folder.lexically_normal() != m_CopySource.parent_path().lexically_normal())
    {
        for (const std::string& file : GltfSiblingFiles(m_CopySource))
        {
            const fs::path from = m_CopySource.parent_path() / EditorUtils::FromUtf8(file);
            const fs::path to = folder / EditorUtils::FromUtf8(file);
            std::error_code companionError;
            if (fs::exists(from, companionError) && !fs::exists(to, companionError))
            {
                fs::copy_file(from, to, fs::copy_options::none, companionError);
                note += companionError ? " (failed to copy " + file + ")" : " (+" + file + ")";
            }
        }
    }

    m_Message = "Pasted " + ToAssetPath(target) + note;
    if (!directory)
    {
        context.SelectAsset(target);
    }
}

void FileBrowserPanel::DuplicateAsset(EditorContext& context, const fs::path& path)
{
    if (!CanEdit(path))
    {
        return;
    }
    // Duplicate leaves the clipboard as it was.
    const fs::path previous = m_CopySource;
    m_CopySource = path;
    PasteInto(context, path.parent_path());
    m_CopySource = previous;
}

bool FileBrowserPanel::IsSelected(const fs::path& path) const
{
    return !path.empty() && std::find(m_Selection.begin(), m_Selection.end(), path) != m_Selection.end();
}

void FileBrowserPanel::SetSelection(EditorContext& context, std::vector<fs::path> paths, const fs::path& primary)
{
    m_Selection.clear();
    for (fs::path& path : paths)
    {
        if (!path.empty() && !IsSelected(path))
        {
            m_Selection.push_back(std::move(path));
        }
    }

    // The inspector shows the primary when it is a file, else the most recently added selected file, else nothing.
    std::error_code error;
    fs::path asset;
    if (IsSelected(primary) && !fs::is_directory(primary, error))
    {
        asset = primary;
    }
    else
    {
        for (auto it = m_Selection.rbegin(); it != m_Selection.rend() && asset.empty(); ++it)
        {
            if (!fs::is_directory(*it, error))
            {
                asset = *it;
            }
        }
    }
    if (asset != context.GetSelectedAsset())
    {
        context.SelectAsset(asset);
    }
    m_SyncedAsset = context.GetSelectedAsset();
}

void FileBrowserPanel::HandleEntryClick(EditorContext& context, const fs::path& path)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && !m_SelectionAnchor.empty())
    {
        // Shift: the items from the anchor to the clicked one, in list order. Ctrl+Shift adds them to the selection.
        const auto anchor = std::find(m_VisibleEntries.begin(), m_VisibleEntries.end(), m_SelectionAnchor);
        const auto target = std::find(m_VisibleEntries.begin(), m_VisibleEntries.end(), path);
        if (anchor != m_VisibleEntries.end() && target != m_VisibleEntries.end())
        {
            std::vector<fs::path> paths = io.KeyCtrl ? m_Selection : std::vector<fs::path>{};
            const auto first = std::min(anchor, target);
            const auto last = std::max(anchor, target);
            paths.insert(paths.end(), first, last + 1);
            SetSelection(context, std::move(paths), path);
            return;
        }
    }

    if (io.KeyCtrl)
    {
        std::vector<fs::path> paths = m_Selection;
        const auto found = std::find(paths.begin(), paths.end(), path);
        if (found != paths.end())
        {
            paths.erase(found);
            const fs::path primary = paths.empty() ? fs::path() : paths.back();
            SetSelection(context, std::move(paths), primary);
        }
        else
        {
            paths.push_back(path);
            SetSelection(context, std::move(paths), path);
        }
    }
    else
    {
        SetSelection(context, { path }, path);
    }
    m_SelectionAnchor = path;
}

void FileBrowserPanel::SyncSelection(EditorContext& context)
{
    // Something outside the list changed the selected asset (paste, references window, a scene object, a refactor).
    const fs::path& asset = context.GetSelectedAsset();
    if (asset != m_SyncedAsset)
    {
        m_SyncedAsset = asset;
        if (asset.empty())
        {
            m_Selection.clear();
        }
        else if (!IsSelected(asset))
        {
            m_Selection = { asset };
            m_SelectionAnchor = asset;
        }
    }

    // The selection only covers what the list shows: leaving the folder or deleting / moving an item drops it.
    std::error_code error;
    m_Selection.erase(std::remove_if(m_Selection.begin(), m_Selection.end(), [&](const fs::path& path)
    {
        return path.parent_path() != m_CurrentDirectory || !fs::exists(path, error);
    }), m_Selection.end());
}

fs::path FileBrowserPanel::GetPrimarySelection(const EditorContext& context) const
{
    if (IsSelected(context.GetSelectedAsset()))
    {
        return context.GetSelectedAsset();
    }
    if (IsSelected(m_SelectionAnchor))
    {
        return m_SelectionAnchor;
    }
    return m_Selection.empty() ? fs::path() : m_Selection.back();
}

std::vector<fs::path> FileBrowserPanel::GetActionTargets(const fs::path& path) const
{
    std::vector<fs::path> targets;
    if (IsSelected(path) && m_Selection.size() > 1)
    {
        for (const fs::path& selected : m_Selection)
        {
            if (CanEdit(selected))
            {
                targets.push_back(selected);
            }
        }
    }
    else if (CanEdit(path))
    {
        targets.push_back(path);
    }
    return targets;
}

void FileBrowserPanel::DrawViewOptions()
{
    EditorSettings& settings = EditorSettings::Get();
    const bool grid = settings.IsContentBrowserGrid();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float sliderWidth = 90.0f;
    const float buttonsWidth = ImGui::CalcTextSize("List").x + ImGui::CalcTextSize("Grid").x + style.FramePadding.x * 4.0f
        + style.ItemSpacing.x + (grid ? sliderWidth + style.ItemSpacing.x : 0.0f);
    // Right-aligned in the remaining width (the header sits in a table column, so absolute x positions do not apply).
    ImGui::SameLine();
    const float spare = ImGui::GetContentRegionAvail().x - buttonsWidth;
    if (spare > 0.0f)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + spare);
    }

    const auto modeButton = [&](const char* label, bool gridMode)
    {
        const bool active = grid == gridMode;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) settings.SetContentBrowserGrid(gridMode);
        if (active) ImGui::PopStyleColor();
    };
    modeButton("List", false);
    ImGui::SameLine();
    modeButton("Grid", true);
    if (grid)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("##IconSize", &m_GridIconSize, 40.0f, 160.0f, "%.0f px");
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            settings.SetContentBrowserGridSize(m_GridIconSize);
        }
    }
}

bool FileBrowserPanel::DrawGridCell(ImTextureID icon, const std::string& label, bool selected, float width, float height, float iconSize)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable("##Cell", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(width, height));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (icon)
    {
        const ImVec2 iconMin(pos.x + (width - iconSize) * 0.5f, pos.y + 4.0f);
        drawList->AddImage(icon, iconMin, ImVec2(iconMin.x + iconSize, iconMin.y + iconSize));
    }

    // Names that do not fit are cut with "..."; the full name is in the tooltip.
    std::string text = label;
    const float maxWidth = width - 4.0f;
    if (ImGui::CalcTextSize(text.c_str()).x > maxWidth)
    {
        while (!text.empty() && ImGui::CalcTextSize((text + "...").c_str()).x > maxWidth)
        {
            // Drop exactly one UTF-8 code point: its continuation bytes, then its lead byte.
            while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
            {
                text.pop_back();
            }
            if (!text.empty())
            {
                text.pop_back();
            }
        }
        text += "...";
    }
    const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
    drawList->AddText(ImVec2(pos.x + (width - textWidth) * 0.5f, pos.y + iconSize + 8.0f),
        ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
    return clicked;
}
