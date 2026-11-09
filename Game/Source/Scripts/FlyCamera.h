#pragma once

#include <Engine.h>

class FlyCamera final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    void SetMoveSpeed(float metersPerSecond) { m_MoveSpeed = metersPerSecond; }
    void SetLookSensitivity(float degreesPerPixel) { m_LookSensitivity = degreesPerPixel; }

    void SetResetKey(Canape::Key key) { m_ResetKey = key; }

    void ReturnHome();

protected:
    void Start() override;
    void Update() override;

private:
    void ApplyPose(
        const Canape::Vector3& position,
        float yaw, float pitch
    ) const;

    float m_MoveSpeed = 5.0f;
    float m_LookSensitivity = 0.003f * Canape::Math::RadToDeg;
    Canape::Key m_ResetKey = Canape::Key::F;

    float m_SpeedScale = 1.0f;
};
