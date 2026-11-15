#include "Panels/FileBrowserPanel.h"
#include "EditorUtils.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
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

    if (!ImGui::Begin(kName))
    {
        ImGui::End();
        return;
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
            DrawDirectoryTree(EditorUtils::GetGameDirectory());
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
    ImGui::End();
}

void FileBrowserPanel::DrawDirectoryTree(const fs::path& directory)
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

    if (open)
    {
        for (const Entry& entry : ListDirectory(directory, true))
        {
            DrawDirectoryTree(entry.path);
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
    if (!m_Message.empty())
    {
        ImGui::TextWrapped("%s", m_Message.c_str());
    }
    ImGui::Separator();

    fs::path nextDirectory;
    for (const Entry& entry : ListDirectory(m_CurrentDirectory, false))
    {
        const std::string label = entry.directory ? entry.name + "/" : entry.name;
        if (entry.directory)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.8f, 0.4f, 1.0f));
        }
        const bool selected = !entry.directory && context.GetSelectedAsset() == entry.path;
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick) && !entry.directory)
        {
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

        if (!entry.directory && IsSourceFile(entry.path) && !GetSourceDirectory(entry.path.parent_path()).empty()
            && ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Delete"))
            {
                m_DeleteSource = entry.path.parent_path() / entry.path.stem();
                m_OpenDeleteSource = true;
            }
            ImGui::EndPopup();
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
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

    if (!nextDirectory.empty())
    {
        m_CurrentDirectory = nextDirectory;
    }

    if (ImGui::BeginPopupContextWindow("##FileBrowserContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::BeginMenu("Create"))
        {
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
        ImGui::EndPopup();
    }

    DrawNewSourcePopup();
    DrawDeleteSourcePopup(context);
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
