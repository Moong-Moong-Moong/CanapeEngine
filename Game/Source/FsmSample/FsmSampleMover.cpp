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
    if (m_StateMachine)
    {
        m_StateMachine->Initialize();
    }
}

void FsmSampleMover::Update()
{
    const Canape::InputManager& input = Canape::Application::Get().GetInputManager();

    float direction = 0.0f;
    if (input.IsKeyDown(Canape::Key::Right)) direction += 1.0f;
    if (input.IsKeyDown(Canape::Key::Left)) direction -= 1.0f;

    if (m_StateMachine)
    {
        m_StateMachine->SetFloat("MoveInput", direction);
        m_StateMachine->SetBool("IsRunning", input.IsKeyDown(Canape::Key::Shift));
        // Evaluated right after this frame's parameters are set.
        m_StateMachine->Update(Canape::Time::DeltaTime());
    }
}
