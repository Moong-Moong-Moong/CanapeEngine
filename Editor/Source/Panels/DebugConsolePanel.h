#pragma once

#include <Engine.h>
#include "CNDebug/DebugLog.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct ImFont;
struct ImGuiInputTextCallbackData;

class DebugConsolePanel
{
public:
    static constexpr const char* kName = "Debug Console";

    void Draw(Canape::CommandRegistry& commands);
    void SetLogFont(ImFont* font) { m_LogFont = font; }

private:
    static constexpr size_t kTagCount = static_cast<size_t>(Canape::Debug::MainTag::Count);

    void DrawToolbar();
    void DrawLog();
    void DrawInput(Canape::CommandRegistry& commands);
    bool Passes(const Canape::Debug::LogEntry& entry) const;

    static int InputCallback(ImGuiInputTextCallbackData* data);
    int HandleInputCallback(ImGuiInputTextCallbackData* data);
    void CompleteCommand(ImGuiInputTextCallbackData* data);

    std::array<bool, kTagCount> m_ShowTags{ true, true, true };
    std::string m_SubTag;
    std::string m_Search;
    uint64_t m_ClearedSequence = 0;
    uint64_t m_LastSequence = 0;
    bool m_AutoScroll = true;
    bool m_ShowSource = false;
    bool m_ScrollToBottom = false;

    std::string m_Input;
    std::vector<std::string> m_History;
    int m_HistoryIndex = -1;
    const Canape::CommandRegistry* m_Commands = nullptr;
    ImFont* m_LogFont = nullptr;
};
