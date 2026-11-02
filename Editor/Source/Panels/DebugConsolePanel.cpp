#include "Panels/DebugConsolePanel.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <set>

using namespace Canape;

namespace
{
    constexpr size_t kMaxInputHistory = 64;

    ImVec4 GetTagColor(Debug::MainTag tag)
    {
        switch (tag)
        {
        case Debug::MainTag::Runtime: return ImVec4(0.45f, 0.85f, 1.0f, 1.0f);
        case Debug::MainTag::DX11Renderer: return ImVec4(0.45f, 1.0f, 0.5f, 1.0f);
        case Debug::MainTag::Game: return ImVec4(1.0f, 0.9f, 0.35f, 1.0f);
        default: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    bool ContainsIgnoreCase(std::string_view text, std::string_view pattern)
    {
        if (pattern.empty())
        {
            return true;
        }
        return std::search(text.begin(), text.end(), pattern.begin(), pattern.end(), [](char lhs, char rhs)
        {
            return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
        }) != text.end();
    }

    std::string FormatEntry(const Debug::LogEntry& entry, bool showSource)
    {
        std::string text = "[";
        text += Debug::GetTagName(entry.tag);
        text += "]";
        if (!entry.subTag.empty())
        {
            text += "[" + entry.subTag + "]";
        }
        text += " " + entry.message;
        if (showSource)
        {
            text += "  (" + entry.file + ":" + std::to_string(entry.line) + ")";
        }
        return text;
    }

    std::string Trim(const std::string& text)
    {
        const size_t begin = text.find_first_not_of(" \t");
        if (begin == std::string::npos)
        {
            return {};
        }
        const size_t end = text.find_last_not_of(" \t");
        return text.substr(begin, end - begin + 1);
    }
}

void DebugConsolePanel::Draw(CommandRegistry& commands)
{
    m_Commands = &commands;
    if (!ImGui::Begin(kName))
    {
        ImGui::End();
        return;
    }

    DrawToolbar();
    ImGui::Separator();
    DrawLog();
    ImGui::Separator();
    DrawInput(commands);
    ImGui::End();
}

void DebugConsolePanel::DrawToolbar()
{
    if (ImGui::Button("Clear"))
    {
        const std::deque<Debug::LogEntry>& history = Debug::GetLogHistory();
        m_ClearedSequence = history.empty() ? m_ClearedSequence : history.back().sequence;
    }

    for (size_t index = 0; index < kTagCount; ++index)
    {
        const Debug::MainTag tag = static_cast<Debug::MainTag>(index);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, GetTagColor(tag));
        if (ImGui::Checkbox(Debug::GetTagName(tag), &m_ShowTags[index]))
        {
            m_SubTag.clear();
        }
        ImGui::PopStyleColor();
    }

    std::set<std::string> subTags;
    for (size_t index = 0; index < kTagCount; ++index)
    {
        if (m_ShowTags[index])
        {
            const std::vector<std::string>& tags = Debug::GetSubTags(static_cast<Debug::MainTag>(index));
            subTags.insert(tags.begin(), tags.end());
        }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("##SubTag", m_SubTag.empty() ? "All SubTags" : m_SubTag.c_str()))
    {
        if (ImGui::Selectable("All SubTags", m_SubTag.empty()))
        {
            m_SubTag.clear();
        }
        for (const std::string& subTag : subTags)
        {
            if (ImGui::Selectable(subTag.c_str(), subTag == m_SubTag))
            {
                m_SubTag = subTag;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##Search", "Search", &m_Search);

    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
    ImGui::SameLine();
    ImGui::Checkbox("Source", &m_ShowSource);
}

void DebugConsolePanel::DrawLog()
{
    const float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    if (!ImGui::BeginChild("##LogScroll", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImGui::EndChild();
        return;
    }

    const std::deque<Debug::LogEntry>& history = Debug::GetLogHistory();
    const uint64_t lastSequence = history.empty() ? m_LastSequence : history.back().sequence;
    const bool hasNewEntries = lastSequence != m_LastSequence;
    m_LastSequence = lastSequence;

    const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();

    if (m_LogFont)
    {
        ImGui::PushFont(m_LogFont);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
    for (const Debug::LogEntry& entry : history)
    {
        if (!Passes(entry))
        {
            continue;
        }

        const std::string text = FormatEntry(entry, m_ShowSource);
        ImGui::PushStyleColor(ImGuiCol_Text, GetTagColor(entry.tag));
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered() && !m_ShowSource)
        {
            ImGui::SetTooltip("%s:%d", entry.file.c_str(), entry.line);
        }

        ImGui::PushID(static_cast<int>(entry.sequence));
        if (ImGui::BeginPopupContextItem("##LogEntry"))
        {
            if (ImGui::MenuItem("Copy"))
            {
                ImGui::SetClipboardText(text.c_str());
            }
            if (!entry.subTag.empty() && ImGui::MenuItem("Filter by SubTag"))
            {
                m_SubTag = entry.subTag;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    if (m_LogFont)
    {
        ImGui::PopFont();
    }

    if (m_ScrollToBottom || (m_AutoScroll && hasNewEntries && wasAtBottom))
    {
        ImGui::SetScrollHereY(1.0f);
    }
    m_ScrollToBottom = false;

    ImGui::EndChild();
}

void DebugConsolePanel::DrawInput(CommandRegistry& commands)
{
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll
        | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion;

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::InputTextWithHint("##Command", "Command (help | Tab: complete | Up/Down: history)", &m_Input, flags,
        &DebugConsolePanel::InputCallback, this))
    {
        return;
    }

    const std::string line = Trim(m_Input);
    m_Input.clear();
    m_HistoryIndex = -1;
    ImGui::SetKeyboardFocusHere(-1);

    if (line.empty())
    {
        return;
    }

    std::erase(m_History, line);
    m_History.push_back(line);
    if (m_History.size() > kMaxInputHistory)
    {
        m_History.erase(m_History.begin());
    }

    commands.Submit(line);
    m_ScrollToBottom = true;
}

bool DebugConsolePanel::Passes(const Debug::LogEntry& entry) const
{
    const size_t tagIndex = static_cast<size_t>(entry.tag);
    if (entry.sequence <= m_ClearedSequence || tagIndex >= kTagCount || !m_ShowTags[tagIndex])
    {
        return false;
    }
    if (!m_SubTag.empty() && entry.subTag != m_SubTag)
    {
        return false;
    }
    return ContainsIgnoreCase(entry.message, m_Search) || ContainsIgnoreCase(entry.subTag, m_Search);
}

int DebugConsolePanel::InputCallback(ImGuiInputTextCallbackData* data)
{
    return static_cast<DebugConsolePanel*>(data->UserData)->HandleInputCallback(data);
}

int DebugConsolePanel::HandleInputCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
    {
        CompleteCommand(data);
        return 0;
    }

    if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || m_History.empty())
    {
        return 0;
    }

    const int previous = m_HistoryIndex;
    if (data->EventKey == ImGuiKey_UpArrow)
    {
        m_HistoryIndex = m_HistoryIndex < 0 ? static_cast<int>(m_History.size()) - 1 : std::max(m_HistoryIndex - 1, 0);
    }
    else if (data->EventKey == ImGuiKey_DownArrow && m_HistoryIndex >= 0)
    {
        m_HistoryIndex = m_HistoryIndex + 1 < static_cast<int>(m_History.size()) ? m_HistoryIndex + 1 : -1;
    }

    if (previous != m_HistoryIndex)
    {
        const std::string& text = m_HistoryIndex >= 0 ? m_History[static_cast<size_t>(m_HistoryIndex)] : std::string();
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, text.c_str());
    }
    return 0;
}

void DebugConsolePanel::CompleteCommand(ImGuiInputTextCallbackData* data)
{
    if (!m_Commands)
    {
        return;
    }

    const std::string_view typed(data->Buf, static_cast<size_t>(data->CursorPos));
    if (typed.find(' ') != std::string_view::npos)
    {
        return;
    }

    std::vector<const CommandRegistry::Command*> matches;
    for (const CommandRegistry::Command& command : m_Commands->GetCommands())
    {
        if (command.name.starts_with(typed))
        {
            matches.push_back(&command);
        }
    }
    if (matches.empty())
    {
        return;
    }

    std::string completion = matches.front()->name;
    for (const CommandRegistry::Command* command : matches)
    {
        size_t length = 0;
        while (length < completion.size() && length < command->name.size() && completion[length] == command->name[length])
        {
            ++length;
        }
        completion.resize(length);
    }
    if (matches.size() == 1)
    {
        completion += ' ';
    }

    if (completion.size() > typed.size())
    {
        data->DeleteChars(0, data->CursorPos);
        data->InsertChars(0, completion.c_str());
    }
    else if (matches.size() > 1)
    {
        for (const CommandRegistry::Command* command : matches)
        {
            CN_LOG_RUNTIME("Console", command->usage, " - ", command->description);
        }
        m_ScrollToBottom = true;
    }
}
