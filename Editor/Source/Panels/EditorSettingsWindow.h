#pragma once

#include "Icons/EditorIcons.h"

#include <string>
#include <vector>

// Tools > Editor Settings: general options (undo history size) and icon overrides.
class EditorSettingsWindow
{
public:
    static constexpr const char* kName = "Editor Settings";

    void Open() { m_Open = true; m_FocusRequested = true; }
    void Draw();

private:
    void DrawGeneralPage();
    void DrawIconsPage();
    void DrawIconTable(const char* id, const std::vector<EditorIcons::Entry>& entries);
    void DrawIconRow(const EditorIcons::Entry& entry);
    static bool BrowseForImage(std::string& result);

    bool m_Open = false;
    bool m_FocusRequested = false;
    bool m_ScrollToTop = false;
    std::string m_Filter;
    std::vector<EditorIcons::Entry> m_ComponentTypes;
};
