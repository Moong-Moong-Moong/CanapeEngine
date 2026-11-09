#include "Panels/StateMachineInspector.h"
#include "EditorUtils.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <filesystem>
#include <regex>
#include <utility>
#include <vector>

using namespace Canape;

// UI only: every change to the definition goes through the StateMachine API.
namespace
{
    const ImVec4 kWarningColor(1.0f, 0.45f, 0.35f, 1.0f);
    const ImVec4 kActiveColor(0.45f, 0.9f, 0.45f, 1.0f);

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

    bool CompareCombo(FsmCompare& compare, const std::vector<FsmCompare>& options)
    {
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

    //*** State class file generation (editor tooling, not FSM logic)

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

    std::filesystem::path GetStateHeaderPath(const std::string& folder, const std::string& className)
    {
        std::filesystem::path path = EditorUtils::GetGameDirectory() / "Source";
        if (!folder.empty())
        {
            path /= EditorUtils::FromUtf8(folder);
        }
        return (path / (className + ".h")).lexically_normal();
    }

    // Runs Tools/NewScript.ps1 -State. Returns false with a message on failure.
    bool RunNewStateTool(const std::string& className, const std::string& folder, std::string& error)
    {
        std::wstring arguments = L"-State -Name " + EditorUtils::FromUtf8(className).wstring();
        if (!folder.empty())
        {
            arguments += L" -Folder " + EditorUtils::FromUtf8(folder).wstring();
        }
        return EditorUtils::RunTool("NewScript.ps1", arguments, error);
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
    // Copied, since the API calls below change the list.
    const std::vector<FsmParameterDesc> parameters = stateMachine.GetParameterDescs();
    const bool open = ImGui::TreeNodeEx("##parameters", ImGuiTreeNodeFlags_DefaultOpen, "Parameters (%zu)", parameters.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addParameter"))
    {
        stateMachine.AddParameter();
    }
    if (!open)
    {
        return;
    }

    std::string removeName;
    if (ImGui::BeginTable("##parameterTable", 4, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed);

        for (int index = 0; index < static_cast<int>(parameters.size()); ++index)
        {
            const FsmParameterDesc& parameter = parameters[index];
            ImGui::PushID(index);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string name = parameter.name;
            if (EditName("##name", name))
            {
                stateMachine.RenameParameter(parameter.name, name);
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##type", ToString(parameter.type)))
            {
                for (FsmParameterType type : { FsmParameterType::Float, FsmParameterType::Int, FsmParameterType::Bool })
                {
                    if (ImGui::Selectable(ToString(type), type == parameter.type))
                    {
                        stateMachine.SetParameterType(parameter.name, type);
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
                    stateMachine.SetParameterDefault(parameter.name, value ? 1.0f : 0.0f);
                }
            }
            else if (parameter.type == FsmParameterType::Int)
            {
                int value = static_cast<int>(parameter.defaultValue);
                if (ImGui::DragInt("##default", &value))
                {
                    stateMachine.SetParameterDefault(parameter.name, static_cast<float>(value));
                }
            }
            else
            {
                float value = parameter.defaultValue;
                if (ImGui::DragFloat("##default", &value, 0.05f))
                {
                    stateMachine.SetParameterDefault(parameter.name, value);
                }
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X"))
            {
                removeName = parameter.name;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (!removeName.empty())
    {
        stateMachine.RemoveParameter(removeName);
    }
    ImGui::TreePop();
}

void StateMachineInspector::DrawConditions(StateMachine& stateMachine)
{
    const std::vector<FsmConditionDesc> conditions = stateMachine.GetConditionDescs();
    const bool sectionOpen = ImGui::TreeNodeEx("##conditions", ImGuiTreeNodeFlags_DefaultOpen, "Conditions (%zu)", conditions.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addCondition"))
    {
        stateMachine.AddCondition();
    }
    if (!sectionOpen)
    {
        return;
    }

    const std::vector<std::string> parameterNames = stateMachine.GetParameterNames();

    std::string removeName;
    for (int index = 0; index < static_cast<int>(conditions.size()); ++index)
    {
        const FsmConditionDesc& condition = conditions[index];
        ImGui::PushID(index);

        // The header row is the arrow, the editable name, the add-comparison button and the delete button.
        const bool open = ImGui::TreeNodeEx("##condition", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::SameLine();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonsWidth = ImGui::CalcTextSize("+").x + ImGui::CalcTextSize("X").x + style.FramePadding.x * 4.0f + style.ItemSpacing.x * 2.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonsWidth);
        std::string name = condition.name;
        if (EditName("##name", name))
        {
            stateMachine.RenameCondition(condition.name, name);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+##addComparison"))
        {
            stateMachine.AddComparison(condition.name);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))
        {
            removeName = condition.name;
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
                    const FsmCondition& comparison = condition.comparisons[row];
                    ImGui::PushID(row);
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    std::string parameterName = comparison.parameter;
                    if (NameCombo("##parameter", parameterName, parameterNames))
                    {
                        stateMachine.SetComparisonParameter(condition.name, row, parameterName);
                    }

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    FsmCompare compare = comparison.compare;
                    if (CompareCombo(compare, stateMachine.GetCompareOptions(comparison.parameter)))
                    {
                        stateMachine.SetComparisonCompare(condition.name, row, compare);
                    }

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (comparison.compare != FsmCompare::True && comparison.compare != FsmCompare::False)
                    {
                        const FsmParameterDesc* parameter = stateMachine.FindParameterDesc(comparison.parameter);
                        if (parameter && parameter->type == FsmParameterType::Int)
                        {
                            int value = static_cast<int>(comparison.threshold);
                            if (ImGui::DragInt("##threshold", &value))
                            {
                                stateMachine.SetComparisonThreshold(condition.name, row, static_cast<float>(value));
                            }
                        }
                        else
                        {
                            float value = comparison.threshold;
                            if (ImGui::DragFloat("##threshold", &value, 0.05f))
                            {
                                stateMachine.SetComparisonThreshold(condition.name, row, value);
                            }
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
                stateMachine.RemoveComparison(condition.name, removeComparison);
            }
            if (condition.comparisons.empty())
            {
                ImGui::TextDisabled("No comparisons: always passes.");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (!removeName.empty())
    {
        stateMachine.RemoveCondition(removeName);
    }
    ImGui::TreePop();
}

void StateMachineInspector::DrawStates(StateMachine& stateMachine)
{
    if (!ImGui::TreeNodeEx("##states", ImGuiTreeNodeFlags_DefaultOpen, "States (%zu)", stateMachine.GetStateDescs().size()))
    {
        return;
    }

    const std::vector<std::string> roots = stateMachine.GetRootStateNames();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    std::string initial = stateMachine.GetInitialState();
    if (NameCombo("Initial State", initial, roots))
    {
        stateMachine.SetInitialState(initial);
    }

    for (const std::string& root : roots)
    {
        DrawStateNode(stateMachine, root);
    }

    // Removed after the tree is drawn, since the tree walks the state list.
    if (!m_RemoveState.empty())
    {
        const std::string name = std::exchange(m_RemoveState, {});
        if (stateMachine.RemoveState(name) && m_SelectedState == name)
        {
            m_SelectedState.clear();
        }
    }

    DrawSelectedState(stateMachine);
    ImGui::TreePop();
}

void StateMachineInspector::DrawStateNode(StateMachine& stateMachine, const std::string& name)
{
    const FsmStateDesc* state = stateMachine.FindStateDesc(name);
    if (!state)
    {
        return;
    }

    const std::vector<std::string> children = stateMachine.GetChildStateNames(name);
    const bool initial = stateMachine.IsInitialState(name);
    const bool active = stateMachine.IsStateActive(name);
    const bool compiled = StateMachine::IsStateClassRegistered(state->type);

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

    // RemoveState refuses a state with children, so the button is disabled for them.
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
            DrawStateNode(stateMachine, child);
        }
        ImGui::TreePop();
    }
}

void StateMachineInspector::DrawSelectedState(StateMachine& stateMachine)
{
    const FsmStateDesc* found = stateMachine.FindStateDesc(m_SelectedState);
    if (!found)
    {
        return;
    }
    // Copied, since the API calls below change the list.
    const FsmStateDesc state = *found;

    ImGui::Separator();
    ImGui::Text("State: %s  [%s]", state.name.c_str(), state.type.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("Parent: %s", state.parent.empty() ? "(root)" : state.parent.c_str());

    const std::vector<std::string> children = stateMachine.GetChildStateNames(state.name);
    std::vector<std::string> targets = stateMachine.GetStateNames();
    std::erase(targets, state.name);

    if (!children.empty())
    {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        std::string initialChild = state.initialChild;
        if (NameCombo("Initial Child", initialChild, children))
        {
            stateMachine.SetStateInitialChild(state.name, initialChild);
        }
    }

    const std::vector<std::string> conditionNames = stateMachine.GetConditionNames();
    int removeIndex = -1;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Transitions");
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addTransition"))
    {
        stateMachine.AddTransition(state.name);
    }

    if (!state.transitions.empty() && ImGui::BeginTable("##transitions", 3, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("Condition", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (int index = 0; index < static_cast<int>(state.transitions.size()); ++index)
        {
            const FsmTransitionDesc& transition = state.transitions[index];
            ImGui::PushID(index);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string target = transition.target;
            if (NameCombo("##target", target, targets))
            {
                stateMachine.SetTransitionTarget(state.name, index, target);
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string condition = transition.condition;
            if (NameCombo("##condition", condition, conditionNames))
            {
                stateMachine.SetTransitionCondition(state.name, index, condition);
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
        stateMachine.RemoveTransition(state.name, removeIndex);
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
    NameCombo("Parent", m_NewStateParent, stateMachine.GetStateNames());

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
    const std::string className = m_NewStateClass.empty() ? m_NewStateName : m_NewStateClass;

    // Checked before any file is made, so a rejected state leaves no files behind.
    if (!stateMachine.CanAddState(m_NewStateName, m_NewStateParent, m_Message))
    {
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
    const bool compiled = StateMachine::IsStateClassRegistered(className);
    bool created = false;
    if (!compiled && !std::filesystem::exists(GetStateHeaderPath(m_NewStateFolder, className)))
    {
        if (!RunNewStateTool(className, m_NewStateFolder, m_Message))
        {
            return;
        }
        created = true;
    }

    if (!stateMachine.AddState(m_NewStateName, className, m_NewStateParent, m_Message))
    {
        return;
    }

    m_Message = created
        ? "Created " + className + ".h/.cpp. Rebuild the editor before playing."
        : compiled ? "Added '" + m_NewStateName + "'." : "Added '" + m_NewStateName + "'. Rebuild the editor before playing.";
    m_SelectedState = m_NewStateName;
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
