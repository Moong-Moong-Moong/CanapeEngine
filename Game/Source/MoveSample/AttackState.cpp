#include "MoveSample/AttackState.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/AnimatorComponent.h"
#include "FSM/StateMachine.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<AttackState>("AttackState")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void AttackState::Initialize()
{
    m_animator = GetScene()->FindGameObjectByName("Knight")->GetComponent<AnimatorComponent>();
}

void AttackState::Enter()
{
    m_animator->SetSpeed(2.0f);
    GetStateMachine()->SetBool("attackEnd", false);
    PlayCombo(0);
}

void AttackState::Update(float deltaTime)
{
    if (!m_animator)
    {
        GetStateMachine()->SetBool("attackEnd", true);
        return;
    }

    const bool hasNextCombo = m_comboIndex + 1 < kComboCount;
    const bool clipFinished = !m_animator->IsPlaying();

    if (hasNextCombo && GetStateMachine()->GetBool("attackInput") && (clipFinished || GetNormalizedTime() >= m_inputOpenRatio))
    {
        m_inputBuffered = true;
    }

    if (m_inputBuffered && (clipFinished || GetNormalizedTime() >= m_comboCancelRatio))
    {
        PlayCombo(m_comboIndex + 1);
        return;
    }

    if (!clipFinished)
    {
        return;
    }

    m_graceTimer += deltaTime;
    if (!hasNextCombo || m_graceTimer >= m_comboGraceTime)
    {
        GetStateMachine()->SetBool("attackEnd", true);
    }
}

void AttackState::Exit()
{
    m_animator->SetSpeed(1.0f);
    m_inputBuffered = false;
}

void AttackState::PlayCombo(int comboIndex)
{
    m_comboIndex = comboIndex;
    m_inputBuffered = false;
    m_graceTimer = 0.0f;

    if (!m_animator) return;
    m_animator->SetClip(m_comboClips[comboIndex]);
    m_animator->Play(false);
}

float AttackState::GetNormalizedTime() const
{
    const float duration = m_animator->GetDuration();
    return duration > 0.0f ? m_animator->GetTime() / duration : 1.0f;
}
