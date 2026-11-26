#pragma once

#include <filesystem>
#include <map>
#include <string>

// Per-user editor preferences, stored in Editor/Config/EditorSettings.json (git-ignored).
class EditorSettings
{
public:
    static EditorSettings& Get();

    void Load();
    void Save() const;

    // Icon overrides: icon key -> image path (game-relative, repo-relative or absolute, UTF-8).
    const std::string* FindIconOverride(const std::string& key) const;
    void SetIconOverride(const std::string& key, std::string path);
    void ClearIconOverride(const std::string& key);
    void ClearAllIconOverrides();
    const std::map<std::string, std::string>& GetIconOverrides() const { return m_IconOverrides; }

    bool IsContentBrowserGrid() const { return m_ContentBrowserGrid; }
    void SetContentBrowserGrid(bool grid);
    float GetContentBrowserGridSize() const { return m_ContentBrowserGridSize; }
    void SetContentBrowserGridSize(float size);

    static constexpr int kMinUndoLimit = 1;
    static constexpr int kMaxUndoLimit = 100;
    int GetUndoLimit() const { return m_UndoLimit; }
    void SetUndoLimit(int limit);

    static std::filesystem::path GetRepositoryDirectory();

private:
    static std::filesystem::path GetPath();

    std::map<std::string, std::string> m_IconOverrides;
    bool m_ContentBrowserGrid = false;
    float m_ContentBrowserGridSize = 72.0f;
    int m_UndoLimit = 5;
};
