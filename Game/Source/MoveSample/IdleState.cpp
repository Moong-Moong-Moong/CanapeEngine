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
    Handle<GameObject> handle = GetOwnerObject()->GetScene()->FindGameObjectByName("MasterChief_v5.7_walk_scl");
    m_animator = GetOwnerObject()->GetScene()->GetGameObject(handle)->GetComponent<AnimatorComponent>();

}

void IdleState::Enter()
{
    if (m_animator)
    {
        m_animator->Stop();
    }
}

void IdleState::Update([[maybe_unused]] float deltaTime)
{
}

void IdleState::Exit()
{
}
