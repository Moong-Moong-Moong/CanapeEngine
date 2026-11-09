#pragma once

#include "EditorContext.h"

#include <filesystem>
#include <string>

class FileBrowserPanel
{
public:
    static constexpr const char* kName = "File Browser";

    void Draw(EditorContext& context);

private:
    enum class SourceKind
    {
        ScriptComponent,
        FsmState,
    };

    void DrawDirectoryTree(const std::filesystem::path& directory);
    void DrawDirectoryContents(EditorContext& context);
    void DrawNewSourcePopup();
    void DrawDeleteSourcePopup(EditorContext& context);
    void CreateMaterial(EditorContext& context);

    std::filesystem::path m_CurrentDirectory;

    // C++ source create / delete
    bool m_OpenNewSource = false;
    SourceKind m_NewSourceKind = SourceKind::ScriptComponent;
    std::string m_NewSourceName;
    bool m_OpenDeleteSource = false;
    std::filesystem::path m_DeleteSource; // Without extension.
    std::string m_Message;
};
