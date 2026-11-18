#pragma once

#include <Engine.h>

// Pushes the water at its position, once every interval.
class RippleSource final: public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

private:
    // Seconds.
    float interval = 2.0f;
    float radius = 1.5f;
    // Meters per second, up when positive.
    float strength = -6.0f;

    // Runtime state, not registered.
    float waited = 0.0f;

public:
    RippleSource() = default;

protected:
    void Update() override;
};
