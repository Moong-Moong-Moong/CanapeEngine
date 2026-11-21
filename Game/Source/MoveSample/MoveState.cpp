#include "MoveSample/MoveState.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/AnimatorComponent.h"
#include "input/InputManager.h"
#include "Core/Application.h"
#include "MoveSample/MoveAnimationController.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<MoveState>("MoveState")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void MoveState::Initialize()
{
    m_animator = GetScene()->FindGameObjectByName("Knight")->GetComponent<AnimatorComponent>();
    m_player = GetOwnerObject()->GetComponent<MoveAnimationController>();
}

void MoveState::Enter()
{
    if (!m_animator) return;
    m_animator->Play();
}

void MoveState::Update([[maybe_unused]] float deltaTime)
{
    if (!m_animator) return;
    if (Application::Get().GetInputManager().IsKeyDown(Key::Shift))
    {
        m_player->SetMoveSpeed(6.0f);
        m_animator->SetClip("Content/Models/Knight/run.aclip");
    }
    else
    {
        m_player->SetMoveSpeed(3.0f);
        m_animator->SetClip("Content/Models/Knight/walk.aclip");
    }
}

void MoveState::Exit()
{
}
