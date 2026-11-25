#include "EditorSettings.h"
#include "EditorUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

EditorSettings& EditorSettings::Get()
{
    static EditorSettings settings;
    return settings;
}

fs::path EditorSettings::GetRepositoryDirectory()
{
    return (EditorUtils::GetGameDirectory() / "..").lexically_normal();
}

fs::path EditorSettings::GetPath()
{
    return GetRepositoryDirectory() / "Editor" / "Config" / "EditorSettings.json";
}

void EditorSettings::Load()
{
    std::ifstream file(GetPath(), std::ios::binary);
    if (!file)
    {
        return;
    }
    const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return;
    }

    m_IconOverrides.clear();
    if (const auto icons = root.find("icons"); icons != root.end() && icons->is_object())
    {
        for (const auto& [key, value] : icons->items())
        {
            if (value.is_string() && !value.get<std::string>().empty())
            {
                m_IconOverrides[key] = value.get<std::string>();
            }
        }
    }
    if (const auto undo = root.find("undo"); undo != root.end() && undo->is_object())
    {
        m_UndoLimit = std::clamp(undo->value("limit", m_UndoLimit), kMinUndoLimit, kMaxUndoLimit);
    }
    if (const auto browser = root.find("contentBrowser"); browser != root.end() && browser->is_object())
    {
        m_ContentBrowserGrid = browser->value("grid", m_ContentBrowserGrid);
        m_ContentBrowserGridSize = std::clamp(browser->value("gridSize", m_ContentBrowserGridSize), 40.0f, 160.0f);
    }
}

void EditorSettings::Save() const
{
    nlohmann::json root;
    root["icons"] = nlohmann::json::object();
    for (const auto& [key, value] : m_IconOverrides)
    {
        root["icons"][key] = value;
    }
    root["contentBrowser"] = { { "grid", m_ContentBrowserGrid }, { "gridSize", m_ContentBrowserGridSize } };
    root["undo"] = { { "limit", m_UndoLimit } };

    const fs::path path = GetPath();
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (file)
    {
        file << root.dump(4);
    }
}

const std::string* EditorSettings::FindIconOverride(const std::string& key) const
{
    const auto found = m_IconOverrides.find(key);
    return found != m_IconOverrides.end() ? &found->second : nullptr;
}

void EditorSettings::SetIconOverride(const std::string& key, std::string path)
{
    m_IconOverrides[key] = std::move(path);
    Save();
}

void EditorSettings::ClearIconOverride(const std::string& key)
{
    if (m_IconOverrides.erase(key) > 0)
    {
        Save();
    }
}

void EditorSettings::ClearAllIconOverrides()
{
    m_IconOverrides.clear();
    Save();
}

void EditorSettings::SetContentBrowserGrid(bool grid)
{
    if (m_ContentBrowserGrid != grid)
    {
        m_ContentBrowserGrid = grid;
        Save();
    }
}

void EditorSettings::SetUndoLimit(int limit)
{
    limit = std::clamp(limit, kMinUndoLimit, kMaxUndoLimit);
    if (m_UndoLimit != limit)
    {
        m_UndoLimit = limit;
        Save();
    }
}

void EditorSettings::SetContentBrowserGridSize(float size)
{
    size = std::clamp(size, 40.0f, 160.0f);
    if (m_ContentBrowserGridSize != size)
    {
        m_ContentBrowserGridSize = size;
        Save();
    }
}
