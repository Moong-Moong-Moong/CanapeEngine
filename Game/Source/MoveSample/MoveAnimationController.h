#pragma once

#include <Engine.h>

using namespace Canape;

class MoveAnimationController final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

protected:
    void Awake() override;
    void Start() override;
    void FixedUpdate() override;
    void Update() override;
    void LateUpdate() override;

public:
    void SetMoveSpeed(float speed);

private:
    void TurnTowards(const Vector3& direction);

    AnimatorComponent* m_animator = nullptr;
    StateMachine* m_stateMachine = nullptr;

    float m_moveSpeed = 3.0f;
    float m_turnSpeed = 720.0f;
};
