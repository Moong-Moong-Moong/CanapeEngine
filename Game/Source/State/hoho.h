#pragma once

#include <Engine.h>

class hoho final : public Canape::FsmState
{
    RTTR_ENABLE(Canape::FsmState)
    RTTR_REGISTRATION_FRIEND

public:
    void Enter() override;
    void Update(float deltaTime) override;
    void Exit() override;
};
