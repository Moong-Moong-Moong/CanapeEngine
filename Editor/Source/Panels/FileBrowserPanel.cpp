#include "Panels/FileBrowserPanel.h"
#include "EditorUtils.h"

#include <imgui.h>

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

    if (ImGui::BeginPopupContextWindow("##FileBrowserContext"))
    {
        if (ImGui::BeginMenu("Create"))
        {
            if (ImGui::MenuItem("Material"))
            {
                CreateMaterial(context);
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
}

void FileBrowserPanel::CreateMaterial(EditorContext& context)
{
    std::error_code error;
    fs::path file = m_CurrentDirectory / (std::string("NewMaterial") + Canape::MaterialFile::kExtension);
    for (int index = 1; fs::exists(file, error); ++index)
    {
        file = m_CurrentDirectory / ("NewMaterial" + std::to_string(index) + Canape::MaterialFile::kExtension);
    }

    if (Canape::MaterialFile::Save(file, Canape::MaterialFileData{}))
    {
        context.SelectAsset(file);
    }
}
