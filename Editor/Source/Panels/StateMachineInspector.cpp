#include "Panels/StateMachineInspector.h"
#include "EditorUtils.h"

#include <imgui_stdlib.h>

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <regex>
#include <utility>
#include <vector>

using namespace Canape;

namespace
{
    const ImVec4 kWarningColor(1.0f, 0.45f, 0.35f, 1.0f);
    const ImVec4 kActiveColor(0.45f, 0.9f, 0.45f, 1.0f);

    template <typename T>
    T* FindByName(std::vector<T>& items, const std::string& name)
    {
        const auto it = std::find_if(items.begin(), items.end(), [&name](const T& item) { return item.name == name; });
        return it != items.end() ? &*it : nullptr;
    }

    template <typename T>
    std::vector<std::string> Names(const std::vector<T>& items)
    {
        std::vector<std::string> names;
        for (const T& item : items)
        {
            names.push_back(item.name);
        }
        return names;
    }

    template <typename T>
    std::string MakeUniqueName(const std::vector<T>& items, const std::string& base)
    {
        std::string name = base;
        for (int index = 1; std::any_of(items.begin(), items.end(), [&name](const T& item) { return item.name == name; }); ++index)
        {
            name = base + std::to_string(index);
        }
        return name;
    }

    bool IsIdentifier(const std::string& text)
    {
        static const std::regex pattern("^[A-Za-z_][A-Za-z0-9_]*$");
        return std::regex_match(text, pattern);
    }

    bool IsFolder(const std::string& text)
    {
        static const std::regex pattern("^[A-Za-z0-9_]+([\\\\/][A-Za-z0-9_]+)*$");
        return text.empty() || std::regex_match(text, pattern);
    }

    // Picks one of the names. A name that no longer exists is shown in the warning color.
    bool NameCombo(const char* label, std::string& value, const std::vector<std::string>& options)
    {
        const bool missing = !value.empty() && std::find(options.begin(), options.end(), value) == options.end();
        const std::string preview = value.empty() ? "(none)" : missing ? value + " (missing)" : value;

        if (missing)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
        }
        const bool open = ImGui::BeginCombo(label, preview.c_str());
        if (missing)
        {
            ImGui::PopStyleColor();
        }
        if (!open)
        {
            return false;
        }

        bool changed = false;
        if (ImGui::Selectable("(none)", value.empty()))
        {
            value.clear();
            changed = true;
        }
        for (const std::string& option : options)
        {
            if (ImGui::Selectable(option.c_str(), option == value))
            {
                value = option;
                changed = true;
            }
        }
        ImGui::EndCombo();
        return changed;
    }

    const char* ToString(FsmCompare compare)
    {
        switch (compare)
        {
        case FsmCompare::Greater:  return "Greater";
        case FsmCompare::Less:     return "Less";
        case FsmCompare::Equal:    return "Equal";
        case FsmCompare::NotEqual: return "NotEqual";
        case FsmCompare::True:     return "True";
        case FsmCompare::False:    return "False";
        }
        return "?";
    }

    const char* ToString(FsmParameterType type)
    {
        switch (type)
        {
        case FsmParameterType::Float: return "Float";
        case FsmParameterType::Int:   return "Int";
        case FsmParameterType::Bool:  return "Bool";
        }
        return "?";
    }

    // Offers only the comparisons that make sense for the parameter type.
    bool CompareCombo(FsmCompare& compare, const FsmParameterDesc* parameter)
    {
        std::vector<FsmCompare> options;
        if (!parameter || parameter->type != FsmParameterType::Bool)
        {
            options = { FsmCompare::Greater, FsmCompare::Less, FsmCompare::Equal, FsmCompare::NotEqual };
        }
        if (!parameter || parameter->type == FsmParameterType::Bool)
        {
            options.push_back(FsmCompare::True);
            options.push_back(FsmCompare::False);
        }

        if (!ImGui::BeginCombo("##compare", ToString(compare)))
        {
            return false;
        }
        bool changed = false;
        for (FsmCompare option : options)
        {
            if (ImGui::Selectable(ToString(option), option == compare))
            {
                compare = option;
                changed = true;
            }
        }
        ImGui::EndCombo();
        return changed;
    }

    bool IsStateClassCompiled(const std::string& className)
    {
        const rttr::type type = rttr::type::get_by_name(className);
        return type.is_valid() && type.is_derived_from<FsmState>();
    }

    std::filesystem::path GetStateHeaderPath(const std::string& folder, const std::string& className)
    {
        std::filesystem::path path = EditorUtils::GetGameDirectory() / "Source";
        if (!folder.empty())
        {
            path /= EditorUtils::FromUtf8(folder);
        }
        return (path / (className + ".h")).lexically_normal();
    }

    // Runs Tools/NewScript.ps1 -State and waits for it. Returns false with a message on failure.
    bool RunNewStateTool(const std::string& className, const std::string& folder, std::string& error)
    {
        const std::filesystem::path script = (EditorUtils::GetGameDirectory() / ".." / "Tools" / "NewScript.ps1").lexically_normal();
        std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring()
            + L"\" -State -Name " + EditorUtils::FromUtf8(className).wstring();
        if (!folder.empty())
        {
            command += L" -Folder " + EditorUtils::FromUtf8(folder).wstring();
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        {
            error = "Could not start NewScript.ps1";
            return false;
        }

        WaitForSingleObject(process.hProcess, 30000);
        DWORD exitCode = 1;
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        if (exitCode != 0)
        {
            error = "NewScript.ps1 failed (exit " + std::to_string(exitCode) + ")";
            return false;
        }
        return true;
    }
}

void StateMachineInspector::Draw(StateMachine& stateMachine)
{
    ImGui::PushID(&stateMachine);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    std::string name = stateMachine.GetName();
    if (EditName("##fsmName", name))
    {
        stateMachine.SetName(name);
    }

    const std::string current = stateMachine.GetCurrentStateName();
    ImGui::TextUnformatted("Current State");
    ImGui::SameLine();
    if (current.empty())
    {
        ImGui::TextDisabled("(not running)");
    }
    else
    {
        ImGui::TextColored(kActiveColor, "%s", current.c_str());
    }
    ImGui::Separator();

    DrawParameters(stateMachine);
    DrawConditions(stateMachine);
    DrawStates(stateMachine);
    DrawAddState(stateMachine);

    ImGui::PopID();
}

void StateMachineInspector::DrawParameters(StateMachine& stateMachine)
{
    std::vector<FsmParameterDesc>& parameters = stateMachine.EditParameterDescs();
    const bool open = ImGui::TreeNodeEx("##parameters", ImGuiTreeNodeFlags_DefaultOpen, "Parameters (%zu)", parameters.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addParameter"))
    {
        parameters.push_back({ MakeUniqueName(parameters, "NewParameter") });
    }
    if (!open)
    {
        return;
    }

    int removeIndex = -1;
    if (ImGui::BeginTable("##parameterTable", 4, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed);

        for (int index = 0; index < static_cast<int>(parameters.size()); ++index)
        {
            FsmParameterDesc& parameter = parameters[index];
            ImGui::PushID(index);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string name = parameter.name;
            if (EditName("##name", name) && !name.empty() && name != parameter.name && !FindByName(parameters, name))
            {
                for (FsmConditionDesc& condition : stateMachine.EditConditionDescs())
                {
                    for (FsmCondition& comparison : condition.comparisons)
                    {
                        if (comparison.parameter == parameter.name)
                        {
                            comparison.parameter = name;
                        }
                    }
                }
                parameter.name = name;
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##type", ToString(parameter.type)))
            {
                for (FsmParameterType type : { FsmParameterType::Float, FsmParameterType::Int, FsmParameterType::Bool })
                {
                    if (ImGui::Selectable(ToString(type), type == parameter.type))
                    {
                        parameter.type = type;
                        parameter.defaultValue = 0.0f;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (parameter.type == FsmParameterType::Bool)
            {
                bool value = parameter.defaultValue != 0.0f;
                if (ImGui::Checkbox("##default", &value))
                {
                    parameter.defaultValue = value ? 1.0f : 0.0f;
                }
            }
            else if (parameter.type == FsmParameterType::Int)
            {
                int value = static_cast<int>(parameter.defaultValue);
                if (ImGui::DragInt("##default", &value))
                {
                    parameter.defaultValue = static_cast<float>(value);
                }
            }
            else
            {
                ImGui::DragFloat("##default", &parameter.defaultValue, 0.05f);
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X"))
            {
                removeIndex = index;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (removeIndex >= 0)
    {
        parameters.erase(parameters.begin() + removeIndex);
    }
    ImGui::TreePop();
}

void StateMachineInspector::DrawConditions(StateMachine& stateMachine)
{
    std::vector<FsmConditionDesc>& conditions = stateMachine.EditConditionDescs();
    const bool sectionOpen = ImGui::TreeNodeEx("##conditions", ImGuiTreeNodeFlags_DefaultOpen, "Conditions (%zu)", conditions.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addCondition"))
    {
        conditions.push_back({ MakeUniqueName(conditions, "NewCondition") });
    }
    if (!sectionOpen)
    {
        return;
    }

    std::vector<FsmParameterDesc>& parameters = stateMachine.EditParameterDescs();
    const std::vector<std::string> parameterNames = Names(parameters);

    int removeIndex = -1;
    for (int index = 0; index < static_cast<int>(conditions.size()); ++index)
    {
        FsmConditionDesc& condition = conditions[index];
        ImGui::PushID(index);

        // The header row is the arrow, the editable name, the add-comparison button and the delete button.
        const bool open = ImGui::TreeNodeEx("##condition", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::SameLine();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonsWidth = ImGui::CalcTextSize("+").x + ImGui::CalcTextSize("X").x + style.FramePadding.x * 4.0f + style.ItemSpacing.x * 2.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonsWidth);
        std::string name = condition.name;
        if (EditName("##name", name) && !name.empty() && name != condition.name && !FindByName(conditions, name))
        {
            for (FsmStateDesc& state : stateMachine.EditStateDescs())
            {
                for (FsmTransitionDesc& transition : state.transitions)
                {
                    if (transition.condition == condition.name)
                    {
                        transition.condition = name;
                    }
                }
            }
            condition.name = name;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+##addComparison"))
        {
            FsmCondition comparison;
            comparison.compare = FsmCompare::Greater;
            condition.comparisons.push_back(comparison);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))
        {
            removeIndex = index;
        }

        if (open)
        {

            int removeComparison = -1;
            if (ImGui::BeginTable("##comparisons", 4, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableSetupColumn("Compare", ImGuiTableColumnFlags_WidthStretch, 0.3f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.3f);
                ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed);

                for (int row = 0; row < static_cast<int>(condition.comparisons.size()); ++row)
                {
                    FsmCondition& comparison = condition.comparisons[row];
                    ImGui::PushID(row);
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    NameCombo("##parameter", comparison.parameter, parameterNames);
                    const FsmParameterDesc* parameter = FindByName(parameters, comparison.parameter);
                    if (parameter && parameter->type == FsmParameterType::Bool
                        && comparison.compare != FsmCompare::True && comparison.compare != FsmCompare::False)
                    {
                        comparison.compare = FsmCompare::True;
                    }
                    else if (parameter && parameter->type != FsmParameterType::Bool
                        && (comparison.compare == FsmCompare::True || comparison.compare == FsmCompare::False))
                    {
                        comparison.compare = FsmCompare::Greater;
                    }

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    CompareCombo(comparison.compare, parameter);

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (comparison.compare != FsmCompare::True && comparison.compare != FsmCompare::False)
                    {
                        if (parameter && parameter->type == FsmParameterType::Int)
                        {
                            int value = static_cast<int>(comparison.threshold);
                            if (ImGui::DragInt("##threshold", &value))
                            {
                                comparison.threshold = static_cast<float>(value);
                            }
                        }
                        else
                        {
                            ImGui::DragFloat("##threshold", &comparison.threshold, 0.05f);
                        }
                    }

                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("X"))
                    {
                        removeComparison = row;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (removeComparison >= 0)
            {
                condition.comparisons.erase(condition.comparisons.begin() + removeComparison);
            }
            if (condition.comparisons.empty())
            {
                ImGui::TextDisabled("No comparisons: always passes.");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (removeIndex >= 0)
    {
        conditions.erase(conditions.begin() + removeIndex);
    }
    ImGui::TreePop();
}

void StateMachineInspector::DrawStates(StateMachine& stateMachine)
{
    std::vector<FsmStateDesc>& states = stateMachine.EditStateDescs();
    if (!ImGui::TreeNodeEx("##states", ImGuiTreeNodeFlags_DefaultOpen, "States (%zu)", states.size()))
    {
        return;
    }

    std::vector<std::string> roots;
    for (const FsmStateDesc& state : states)
    {
        if (state.parent.empty() || !FindByName(states, state.parent))
        {
            roots.push_back(state.name);
        }
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    std::string initial = stateMachine.GetInitialState();
    if (NameCombo("Initial State", initial, roots))
    {
        stateMachine.SetInitialState(initial);
    }

    // "/Ground/Run/" so a state is active when "/Name/" is found.
    const std::string activePath = "/" + stateMachine.GetCurrentStateName() + "/";
    for (const std::string& root : roots)
    {
        DrawStateNode(stateMachine, root, activePath);
    }

    // Removed after the tree is drawn, since the tree walks the state list.
    if (!m_RemoveState.empty())
    {
        const std::string name = std::exchange(m_RemoveState, {});
        if (stateMachine.GetInitialState() == name)
        {
            stateMachine.SetInitialState("");
        }
        for (FsmStateDesc& other : states)
        {
            if (other.initialChild == name)
            {
                other.initialChild.clear();
            }
        }
        std::erase_if(states, [&name](const FsmStateDesc& desc) { return desc.name == name; });
        if (m_SelectedState == name)
        {
            m_SelectedState.clear();
        }
    }

    DrawSelectedState(stateMachine);
    ImGui::TreePop();
}

void StateMachineInspector::DrawStateNode(StateMachine& stateMachine, const std::string& name, const std::string& activePath)
{
    std::vector<FsmStateDesc>& states = stateMachine.EditStateDescs();
    const FsmStateDesc* state = FindByName(states, name);
    if (!state)
    {
        return;
    }

    std::vector<std::string> children;
    for (const FsmStateDesc& child : states)
    {
        if (child.parent == name)
        {
            children.push_back(child.name);
        }
    }

    const FsmStateDesc* parent = FindByName(states, state->parent);
    const bool initial = parent ? parent->initialChild == name : stateMachine.GetInitialState() == name;
    const bool active = activePath.find("/" + name + "/") != std::string::npos;
    const bool compiled = IsStateClassCompiled(state->type);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
    if (children.empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (m_SelectedState == name)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kActiveColor);
    }
    else if (!compiled)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
    }

    const std::string label = name + "  [" + state->type + "]" + (initial ? "  (initial)" : "") + (compiled ? "" : "  (rebuild needed)");
    const bool open = ImGui::TreeNodeEx(name.c_str(), flags, "%s", label.c_str());

    if (active || !compiled)
    {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        m_SelectedState = name;
    }

    // A parent is deleted after its children so no state is left pointing at a missing parent.
    ImGui::SameLine();
    const float removeWidth = ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - removeWidth);
    ImGui::BeginDisabled(!children.empty());
    if (ImGui::SmallButton("X"))
    {
        m_RemoveState = name;
    }
    ImGui::EndDisabled();
    if (!children.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Delete its children first.");
    }

    if (open)
    {
        for (const std::string& child : children)
        {
            DrawStateNode(stateMachine, child, activePath);
        }
        ImGui::TreePop();
    }
}

void StateMachineInspector::DrawSelectedState(StateMachine& stateMachine)
{
    std::vector<FsmStateDesc>& states = stateMachine.EditStateDescs();
    FsmStateDesc* state = FindByName(states, m_SelectedState);
    if (!state)
    {
        return;
    }

    ImGui::Separator();
    ImGui::Text("State: %s  [%s]", state->name.c_str(), state->type.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("Parent: %s", state->parent.empty() ? "(root)" : state->parent.c_str());

    std::vector<std::string> children;
    std::vector<std::string> targets;
    for (const FsmStateDesc& other : states)
    {
        if (other.parent == state->name)
        {
            children.push_back(other.name);
        }
        if (other.name != state->name)
        {
            targets.push_back(other.name);
        }
    }

    if (!children.empty())
    {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        NameCombo("Initial Child", state->initialChild, children);
    }

    const std::vector<std::string> conditionNames = Names(stateMachine.GetConditionDescs());
    int removeIndex = -1;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Transitions");
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addTransition"))
    {
        state->transitions.push_back({});
    }

    if (!state->transitions.empty() && ImGui::BeginTable("##transitions", 3, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("Condition", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (int index = 0; index < static_cast<int>(state->transitions.size()); ++index)
        {
            FsmTransitionDesc& transition = state->transitions[index];
            ImGui::PushID(index);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            NameCombo("##target", transition.target, targets);

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            NameCombo("##condition", transition.condition, conditionNames);

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X"))
            {
                removeIndex = index;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (removeIndex >= 0)
    {
        state->transitions.erase(state->transitions.begin() + removeIndex);
    }
}

void StateMachineInspector::DrawAddState(StateMachine& stateMachine)
{
    ImGui::Separator();
    if (m_CloseAddState)
    {
        ImGui::SetNextItemOpen(false);
        m_CloseAddState = false;
    }
    if (!ImGui::TreeNodeEx("##addState", ImGuiTreeNodeFlags_None, "Add State"))
    {
        // Keeps the result of the last add (e.g. "rebuild needed") visible after the section closes.
        if (!m_Message.empty())
        {
            ImGui::TextWrapped("%s", m_Message.c_str());
        }
        return;
    }

    const float width = ImGui::GetContentRegionAvail().x * 0.6f;
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("Name", &m_NewStateName);
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("Class", &m_NewStateClass);
    ImGui::SetNextItemWidth(width);
    ImGui::InputTextWithHint("Folder", "under Game/Source", &m_NewStateFolder);
    ImGui::SetNextItemWidth(width);
    NameCombo("Parent", m_NewStateParent, Names(stateMachine.GetStateDescs()));

    const float buttonWidth = ImGui::CalcTextSize("Make Class").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonWidth);
    if (ImGui::Button("Make Class"))
    {
        AddState(stateMachine);
    }
    if (!m_Message.empty())
    {
        ImGui::TextWrapped("%s", m_Message.c_str());
    }
    ImGui::TreePop();
}

void StateMachineInspector::AddState(StateMachine& stateMachine)
{
    std::vector<FsmStateDesc>& states = stateMachine.EditStateDescs();
    const std::string className = m_NewStateClass.empty() ? m_NewStateName : m_NewStateClass;

    if (m_NewStateName.empty() || m_NewStateName.find('/') != std::string::npos)
    {
        m_Message = "Enter a state name without '/'.";
        return;
    }
    if (FindByName(states, m_NewStateName))
    {
        m_Message = "A state named '" + m_NewStateName + "' already exists.";
        return;
    }
    if (!IsIdentifier(className))
    {
        m_Message = "'" + className + "' is not a valid C++ class name.";
        return;
    }
    if (!IsFolder(m_NewStateFolder))
    {
        m_Message = "'" + m_NewStateFolder + "' is not a valid folder.";
        return;
    }

    // An existing class is reused; only a new one gets files.
    bool created = false;
    if (!IsStateClassCompiled(className) && !std::filesystem::exists(GetStateHeaderPath(m_NewStateFolder, className)))
    {
        std::string error;
        if (!RunNewStateTool(className, m_NewStateFolder, error))
        {
            m_Message = error;
            return;
        }
        created = true;
    }

    FsmStateDesc state;
    state.name = m_NewStateName;
    state.type = className;
    state.parent = m_NewStateParent;
    states.push_back(state);

    // The first child or root becomes the one entered by default.
    if (FsmStateDesc* parent = FindByName(states, m_NewStateParent))
    {
        if (parent->initialChild.empty())
        {
            parent->initialChild = state.name;
        }
    }
    else if (stateMachine.GetInitialState().empty())
    {
        stateMachine.SetInitialState(state.name);
    }

    m_Message = created
        ? "Created " + className + ".h/.cpp. Rebuild the editor before playing."
        : IsStateClassCompiled(className) ? "Added '" + state.name + "'." : "Added '" + state.name + "'. Rebuild the editor before playing.";
    m_SelectedState = state.name;
    m_NewStateName.clear();
    m_NewStateClass.clear();
    m_CloseAddState = true;
}

bool StateMachineInspector::EditName(const char* label, std::string& value)
{
    const ImGuiID id = ImGui::GetID(label);
    std::string temporary = value;
    std::string& buffer = m_ActiveTextId == id ? m_ActiveTextBuffer : temporary;

    ImGui::InputText(label, &buffer);
    if (ImGui::IsItemActivated())
    {
        m_ActiveTextId = id;
        m_ActiveTextBuffer = buffer;
    }

    const bool committed = ImGui::IsItemDeactivatedAfterEdit();
    if (committed)
    {
        value = buffer;
    }
    if (ImGui::IsItemDeactivated() && m_ActiveTextId == id)
    {
        m_ActiveTextId = 0;
    }
    return committed;
}
