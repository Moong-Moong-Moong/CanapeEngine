#pragma once

#include <Engine.h>

#include <imgui.h>

#include <string>

// Inspector UI of a StateMachine: current state, parameters, conditions, states and their transitions.
class StateMachineInspector
{
public:
    void Draw(Canape::StateMachine& stateMachine);

private:
    void DrawParameters(Canape::StateMachine& stateMachine);
    void DrawConditions(Canape::StateMachine& stateMachine);
    void DrawStates(Canape::StateMachine& stateMachine);
    void DrawStateNode(Canape::StateMachine& stateMachine, const std::string& name, const std::string& activePath);
    void DrawSelectedState(Canape::StateMachine& stateMachine);
    void DrawAddState(Canape::StateMachine& stateMachine);
    void AddState(Canape::StateMachine& stateMachine);

    // Commits the text when the field loses focus, so renames are applied once.
    bool EditName(const char* label, std::string& value);

    ImGuiID m_ActiveTextId = 0;
    std::string m_ActiveTextBuffer;

    std::string m_SelectedState;
    std::string m_RemoveState;

    std::string m_NewStateName;
    std::string m_NewStateClass;
    std::string m_NewStateFolder;
    std::string m_NewStateParent;
    std::string m_Message;
    bool m_CloseAddState = false;
};
