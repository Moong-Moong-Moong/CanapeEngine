#pragma once

#include <Engine.h>

class SampleState final : public Canape::FsmState
{
    RTTR_ENABLE(Canape::FsmState)
    RTTR_REGISTRATION_FRIEND

public:
    void Initialize() override;
    void Enter() override;
    void Update(float deltaTime) override;
    void Exit() override;
};
