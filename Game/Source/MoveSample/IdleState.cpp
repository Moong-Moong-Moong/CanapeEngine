#include "MoveSample/IdleState.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/AnimatorComponent.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<IdleState>("IdleState")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void IdleState::Initialize()
{
    m_animator = GetScene()->FindGameObjectByName("Knight")->GetComponent<AnimatorComponent>();
}

void IdleState::Enter()
{
    m_idleTimer = 0.0f;

    if (!m_animator) return;
     m_animator->Play();
     m_animator->SetClip("Content/Models/Knight/idle.aclip");
}

void IdleState::Update([[maybe_unused]] float deltaTime)
{
    if (m_idleTimer <= m_longIdleTime)
    {
        m_idleTimer += deltaTime;
    }
    else
    {
        m_animator->SetClip("Content/Models/Knight/longidle.aclip");
    }
}

void IdleState::Exit()
{
}
