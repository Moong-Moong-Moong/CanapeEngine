#pragma once

#include <Engine.h>

class Test final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

protected:
    void Awake() override;
    void Start() override;
    void FixedUpdate() override;
    void Update() override;
};
