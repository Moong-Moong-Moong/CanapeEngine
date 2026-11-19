#pragma once

#include <Engine.h>

class PlayerCamera final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    void SetTarget(Handle<Canape::GameObject> target) { m_Target = target; }

protected:
    void Start() override;
    void LateUpdate() override;

private:
    void Follow(float blend);

    Handle<Canape::GameObject> m_Target;

    float m_Distance = 7.0f;
    float m_Height = 6.0f;
    float m_LookHeight = 1.0f;
    float m_FollowSpeed = 8.0f;
};
