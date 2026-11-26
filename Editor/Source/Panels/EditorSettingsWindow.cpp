#include "Panels/EditorSettingsWindow.h"
#include "EditorSettings.h"
#include "EditorUtils.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <Windows.h>
#include <commdlg.h>

#include <algorithm>

namespace fs = std::filesystem;

namespace
{
    constexpr float kPreviewSize = 32.0f;

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
        return text;
    }

    bool IsImageFile(const fs::path& path)
    {
        const std::string extension = Lower(EditorUtils::ToUtf8(path.extension()));
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp";
    }
}

void EditorSettingsWindow::Draw()
{
    if (!m_Open)
    {
        return;
    }
    if (m_FocusRequested)
    {
        ImGui::SetNextWindowFocus();
        m_FocusRequested = false;
        // Component types can come from game scripts, so the list is rebuilt whenever the window is opened.
        m_ComponentTypes = EditorIcons::GetComponentTypeEntries();
        m_ScrollToTop = true;
    }
    ImGui::SetNextWindowSize(ImVec2(760.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kName, &m_Open))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##EditorSettingsTabs"))
    {
        if (ImGui::BeginTabItem("General"))
        {
            DrawGeneralPage();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Icons"))
        {
            DrawIconsPage();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorSettingsWindow::DrawGeneralPage()
{
    EditorSettings& settings = EditorSettings::Get();
    ImGui::SeparatorText("Undo");
    int limit = settings.GetUndoLimit();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputInt("Undo history size", &limit, 1, 5))
    {
        settings.SetUndoLimit(limit);
    }
    ImGui::TextDisabled("How many steps Ctrl+Z can go back (%d-%d). Each step keeps two copies of the scene in memory,",
        EditorSettings::kMinUndoLimit, EditorSettings::kMaxUndoLimit);
    ImGui::TextDisabled("so large scenes use more memory per step. Lowering it drops the oldest steps right away.");
}

void EditorSettingsWindow::DrawIconsPage()
{
    ImGui::TextWrapped("Click Browse or drop an image (.png/.jpg/.bmp) from the Content Browser onto a row to replace an icon. "
        "Square images around 64-128 px look best. Changes are saved per user in Editor/Config/EditorSettings.json.");
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##IconFilter", "Filter", &m_Filter);
    ImGui::SameLine();
    const bool anyOverride = !EditorSettings::Get().GetIconOverrides().empty();
    ImGui::BeginDisabled(!anyOverride);
    if (ImGui::Button("Reset All"))
    {
        ImGui::OpenPopup("Reset all icons?");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("Reset all icons?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Every icon goes back to the built-in default.");
        if (ImGui::Button("Reset All"))
        {
            EditorSettings::Get().ClearAllIconOverrides();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    if (ImGui::BeginChild("##IconLists"))
    {
        if (m_ScrollToTop)
        {
            ImGui::SetScrollY(0.0f);
            m_ScrollToTop = false;
        }
        if (ImGui::CollapsingHeader("Hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawIconTable("##GeneralIcons", EditorIcons::GetGeneralEntries());
        }
        if (ImGui::CollapsingHeader("Component Kinds", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Used by every component of that kind unless the type has its own icon below.");
            DrawIconTable("##ComponentKindIcons", EditorIcons::GetComponentKindEntries());
        }
        if (ImGui::CollapsingHeader("Component Types"))
        {
            ImGui::TextDisabled("Give a single component type its own icon.");
            DrawIconTable("##ComponentTypeIcons", m_ComponentTypes);
        }
        if (ImGui::CollapsingHeader("Content Browser Files", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawIconTable("##FileIcons", EditorIcons::GetFileEntries());
        }
    }
    ImGui::EndChild();
}

void EditorSettingsWindow::DrawIconTable(const char* id, const std::vector<EditorIcons::Entry>& entries)
{
    const std::string filter = Lower(m_Filter);
    if (!ImGui::BeginTable(id, 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
    {
        return;
    }
    ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, kPreviewSize + 8.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    for (const EditorIcons::Entry& entry : entries)
    {
        if (!filter.empty() && Lower(entry.label).find(filter) == std::string::npos
            && Lower(entry.detail).find(filter) == std::string::npos)
        {
            continue;
        }
        DrawIconRow(entry);
    }
    ImGui::EndTable();
}

void EditorSettingsWindow::DrawIconRow(const EditorIcons::Entry& entry)
{
    EditorIcons& icons = EditorIcons::Get();
    EditorSettings& settings = EditorSettings::Get();
    ImGui::PushID(entry.key.c_str());
    ImGui::TableNextRow(ImGuiTableRowFlags_None, kPreviewSize + 6.0f);

    ImGui::TableSetColumnIndex(0);
    if (const ImTextureID icon = icons.GetByKey(entry.key))
    {
        ImGui::Image(icon, ImVec2(kPreviewSize, kPreviewSize));
    }
    else
    {
        ImGui::Dummy(ImVec2(kPreviewSize, kPreviewSize));
    }
    // The preview accepts an image dragged from the Content Browser.
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorUtils::kAssetPathPayload))
        {
            const std::string path(static_cast<const char*>(payload->Data), payload->DataSize);
            if (IsImageFile(EditorUtils::FromUtf8(path)))
            {
                settings.SetIconOverride(entry.key, path);
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(entry.label.c_str());
    if (!entry.detail.empty())
    {
        ImGui::TextDisabled("%s", entry.detail.c_str());
    }

    ImGui::TableSetColumnIndex(2);
    const std::string* stored = settings.FindIconOverride(entry.key);
    if (!stored)
    {
        ImGui::TextDisabled("Default");
    }
    else if (icons.IsOverrideBroken(entry.key))
    {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Cannot load (using default)");
        ImGui::TextDisabled("%s", stored->c_str());
    }
    else
    {
        ImGui::TextWrapped("%s", stored->c_str());
    }

    ImGui::TableSetColumnIndex(3);
    if (ImGui::Button("Browse..."))
    {
        std::string chosen;
        if (BrowseForImage(chosen))
        {
            settings.SetIconOverride(entry.key, chosen);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!stored);
    if (ImGui::Button("Reset"))
    {
        settings.ClearIconOverride(entry.key);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

bool EditorSettingsWindow::BrowseForImage(std::string& result)
{
    const std::wstring initialDirectory = EditorIcons::GetIconDirectory().wstring();
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags = OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog))
    {
        return false;
    }
    result = EditorIcons::MakeStoredPath(fs::path(file));
    return true;
}
