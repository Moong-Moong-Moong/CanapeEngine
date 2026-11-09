#pragma once

#include <Engine.h>

class FsmSampleMover;

// Moves the owner along MoveInput at the mover's run speed.
class FsmSampleRun final : public Canape::FsmState
{
    RTTR_ENABLE(Canape::FsmState)
    RTTR_REGISTRATION_FRIEND

public:
    void Initialize() override;
    void Enter() override;
    void Update(float deltaTime) override;
    void Exit() override;

private:
    Canape::TransformComponent* m_Transform = nullptr;
    FsmSampleMover* m_Mover = nullptr;
};
