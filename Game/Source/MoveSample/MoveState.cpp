#include "MoveSample/MoveState.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/AnimatorComponent.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<MoveState>("MoveState")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void MoveState::Initialize()
{
    Handle<GameObject> handle = GetOwnerObject()->GetScene()->FindGameObjectByName("MasterChief_v5.7_walk_scl");
    m_animator = GetOwnerObject()->GetScene()->GetGameObject(handle)->GetComponent<AnimatorComponent>();
}

void MoveState::Enter()
{
    if (m_animator)
    {
        m_animator->SetClip("Content/Models/Master/Armature_mixamo.com_Layer0.aclip");
        m_animator->Play(true);
    }
}

void MoveState::Update([[maybe_unused]] float deltaTime)
{
}

void MoveState::Exit()
{
}
