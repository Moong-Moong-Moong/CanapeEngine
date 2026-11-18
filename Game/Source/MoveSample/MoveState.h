#pragma once

#include <Engine.h>

using namespace Canape;

class MoveState final : public Canape::FsmState
{
    RTTR_ENABLE(Canape::FsmState)
    RTTR_REGISTRATION_FRIEND

public:
    void Initialize() override;
    void Enter() override;
    void Update(float deltaTime) override;
    void Exit() override;

private:
    AnimatorComponent* m_animator = nullptr;
};
