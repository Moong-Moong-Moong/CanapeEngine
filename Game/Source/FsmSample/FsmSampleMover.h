#pragma once

#include <Engine.h>

// Reads the arrow keys (Shift to run) into the StateMachine parameters and runs it.
// It also holds the movement settings; the Walk/Run states do the actual moving.
class FsmSampleMover final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    float GetWalkSpeed() const { return m_WalkSpeed; }
    float GetRunSpeed() const { return m_RunSpeed; }

protected:
    void Start() override;
    void Update() override;

private:
    float m_WalkSpeed = 2.0f;
    float m_RunSpeed = 5.0f;
    Canape::StateMachine* m_StateMachine = nullptr;
};
