#pragma once

#include <Engine.h>

// Floats on the swell: follows its height and leans with it under the footprint.
class SwellRider final: public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

private:
    // The part resting on the water, along forward and along right.
    float lengthMeters = 1.0f;
    float widthMeters = 1.0f;

    // Runtime state, not registered: where it floats on a calm sea.
    float restHeight = 0.0f;
    // Unit, world XZ.
    Canape::Vector2 heading { 0.0f, 1.0f };

public:
    SwellRider() = default;

    float GetLengthMeters() const { return lengthMeters; }
    void SetLengthMeters(float meters);
    float GetWidthMeters() const { return widthMeters; }
    void SetWidthMeters(float meters);

protected:
    void Start() override;
    void Update() override;
};
