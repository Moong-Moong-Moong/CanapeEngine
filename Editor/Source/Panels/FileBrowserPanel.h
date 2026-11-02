#pragma once

#include "EditorContext.h"

#include <filesystem>

class FileBrowserPanel
{
public:
    static constexpr const char* kName = "File Browser";

    void Draw(EditorContext& context);

private:
    void DrawDirectoryTree(const std::filesystem::path& directory);
    void DrawDirectoryContents(EditorContext& context);
    void CreateMaterial(EditorContext& context);

    std::filesystem::path m_CurrentDirectory;
};
