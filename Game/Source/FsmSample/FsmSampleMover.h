#pragma once

#include <Engine.h>

// Moves the object left/right with the arrow keys (Shift to run) and feeds the StateMachine parameters.
class FsmSampleMover final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

protected:
    void Start() override;
    void Update() override;

private:
    float m_WalkSpeed = 2.0f;
    float m_RunSpeed = 5.0f;
    Canape::StateMachine* m_StateMachine = nullptr;
};
