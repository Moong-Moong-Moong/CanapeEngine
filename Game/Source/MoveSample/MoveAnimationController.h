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
    // false면 이동/공격 입력을 무시한다(애니메이션은 계속 갱신되어 Idle로 돌아간다). 퍼즐 조작 중 등에 사용.
    void SetInputEnabled(bool enabled) { m_inputEnabled = enabled; }
    bool IsInputEnabled() const { return m_inputEnabled; }

private:
    void TurnTowards(const Vector3& direction);

    AnimatorComponent* m_animator = nullptr;
    StateMachine* m_stateMachine = nullptr;

    float m_moveSpeed = 3.0f;
    float m_turnSpeed = 720.0f;
    bool m_inputEnabled = true; // 런타임 상태, 직렬화 안 함
};
