#include "Panels/FileBrowserPanel.h"
#include "EditorUtils.h"
#include "Asset/Mesh/MeshSerializer.h"
#include "CNDebug/DebugLog.h"

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

    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && m_RenamingPath.empty() && !ImGui::GetIO().WantTextInput)
    {
        const fs::path& selected = context.GetSelectedAsset();
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && CanEdit(selected) && selected.parent_path() == m_CurrentDirectory)
        {
            BeginRename(context.GetSelectedAsset());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && CanEdit(selected) && selected.parent_path() == m_CurrentDirectory)
        {
            RequestDelete(context, selected);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !m_MoveSource.empty())
        {
            m_MoveSource.clear();
            m_Message = "Move cancelled.";
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt)
        {
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
        if (ImGui::BeginChild("##DirectoryContents"))
        {
            DrawDirectoryContents(context);
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }
    DrawPlanPopup(context);
    ImGui::End();

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

void FileBrowserPanel::DrawDirectoryContents(EditorContext& context)
{
    const fs::path& root = EditorUtils::GetGameDirectory();
    const std::string location = EditorUtils::ToGameRelativePath(m_CurrentDirectory);

    ImGui::BeginDisabled(m_CurrentDirectory == root);
    if (ImGui::ArrowButton("##Up", ImGuiDir_Up))
    {
        m_CurrentDirectory = m_CurrentDirectory.parent_path();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted(location == "." ? "Game" : ("Game/" + location).c_str());
    if (!m_MoveSource.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Moving: %s", ToAssetPath(m_MoveSource).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel Move"))
        {
            m_MoveSource.clear();
        }
        ImGui::TextDisabled("Right-click a folder and choose Move Here. Esc cancels.");
    }
    if (!m_Message.empty())
    {
        ImGui::TextWrapped("%s", m_Message.c_str());
    }
    ImGui::Separator();

    fs::path nextDirectory;
    bool renameRowDrawn = false;
    for (const Entry& entry : ListDirectory(m_CurrentDirectory, false))
    {
        if (entry.path == m_RenamingPath)
        {
            renameRowDrawn = true;
            if (m_RenameFocus)
            {
                ImGui::SetKeyboardFocusHere();
                m_RenameFocus = false;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
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
            continue;
        }

        std::string label = entry.directory ? entry.name + "/" : entry.name;
        if (entry.path == m_MoveSource)
        {
            label += "  (moving)";
        }
        if (entry.directory)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.8f, 0.4f, 1.0f));
        }
        const bool selected = !entry.directory && context.GetSelectedAsset() == entry.path;
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick) && !entry.directory)
        {
            if (selected && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && CanEdit(entry.path))
            {
                m_RenameArmedPath = entry.path;
                m_RenameArmTime = ImGui::GetTime();
            }
            context.SelectAsset(entry.path);
        }
        if (entry.directory)
        {
            ImGui::PopStyleColor();
        }

        if (!entry.directory && ImGui::BeginDragDropSource())
        {
            const std::string assetPath = EditorUtils::ToGameRelativePath(entry.path);
            ImGui::SetDragDropPayload(EditorUtils::kAssetPathPayload, assetPath.data(), assetPath.size());
            ImGui::TextUnformatted(assetPath.c_str());
            ImGui::EndDragDropSource();
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
            ImGui::SetTooltip("Double-click to open scene");
        }
    }

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
        if (!m_MoveSource.empty())
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
            BeginMove(path);
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
    if (!m_MoveSource.empty())
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
            BeginMove(folder);
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
    const std::string error = AssetRefactor::ValidateMove(m_Index.GetRoot(), ToAssetPath(m_MoveSource), ToAssetPath(folder));
    if (ImGui::MenuItem(label, nullptr, false, error.empty()))
    {
        MoveTo(context, folder);
    }
    if (!error.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", error.c_str());
    }
    if (ImGui::MenuItem("Cancel Move"))
    {
        m_MoveSource.clear();
    }
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

void FileBrowserPanel::BeginMove(const fs::path& path)
{
    m_MoveSource = path;
    m_Message.clear();
}

void FileBrowserPanel::MoveTo(EditorContext& context, const fs::path& folder)
{
    std::error_code error;
    const bool emptyFolder = fs::is_directory(m_MoveSource, error) && fs::is_empty(m_MoveSource, error);
    const std::string assetPath = ToAssetPath(m_MoveSource);
    const std::string target = ToAssetPath(folder);
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

void FileBrowserPanel::ApplyPlan(EditorContext& context, const RefactorPlan& plan, bool sceneChecked)
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
    m_MoveSource.clear();

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
}

void FileBrowserPanel::RequestDelete(EditorContext& context, const fs::path& path)
{
    const std::string assetPath = ToAssetPath(path);
    const std::string scenePath = context.GetScenePath().empty() ? std::string() : ToAssetPath(fs::path(context.GetScenePath()));
    std::error_code error;
    const PathMove deleted{ assetPath, std::string(), fs::is_directory(path, error) };
    if (!scenePath.empty() && AssetRefactor::Remap(scenePath, deleted))
    {
        m_Message = "Cannot delete the scene that is open in the editor. Open another scene first.";
        return;
    }

    const bool emptyFolder = fs::is_directory(path, error) && fs::is_empty(path, error);
    RequestPlan(context, [this, assetPath] { return AssetRefactor::PlanDelete(m_Index, assetPath); }, !emptyFolder);
}

std::string FileBrowserPanel::RunDeleteCommand(EditorContext& context, const std::string& assetPath)
{
    EnsureIndex();
    const std::string scenePath = context.GetScenePath().empty() ? std::string() : ToAssetPath(fs::path(context.GetScenePath()));
    std::error_code error;
    const PathMove deleted{ assetPath, std::string(), fs::is_directory(AssetPaths::ToAbsolute(m_Index.GetRoot(), assetPath), error) };
    if (!scenePath.empty() && AssetRefactor::Remap(scenePath, deleted))
    {
        m_Message = "Cannot delete the scene that is open in the editor.";
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
