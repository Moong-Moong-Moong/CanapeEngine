#include "FsmSample/FsmSampleMover.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<FsmSampleMover>("FsmSampleMover")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("walkSpeed", &FsmSampleMover::m_WalkSpeed)
        .property("runSpeed", &FsmSampleMover::m_RunSpeed);
}

void FsmSampleMover::Start()
{
    m_StateMachine = GetComponent<Canape::StateMachine>();
}

void FsmSampleMover::Update()
{
    const Canape::InputManager& input = Canape::Application::Get().GetInputManager();

    float direction = 0.0f;
    if (input.IsKeyDown(Canape::Key::Right)) direction += 1.0f;
    if (input.IsKeyDown(Canape::Key::Left)) direction -= 1.0f;

    const bool running = direction != 0.0f && input.IsKeyDown(Canape::Key::Shift);
    const float speed = direction == 0.0f ? 0.0f : (running ? m_RunSpeed : m_WalkSpeed);

    Canape::TransformComponent* transform = GetTransform();
    transform->SetLocalPosition(transform->GetLocalPosition() + Canape::Vector3::Right * (direction * speed * Canape::Time::DeltaTime()));

    if (m_StateMachine)
    {
        m_StateMachine->SetFloat("Speed", speed);
        m_StateMachine->SetBool("IsRunning", running);
    }
}
