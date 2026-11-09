#include "FsmSample/FsmSampleWalk.h"
#include "FsmSample/FsmSampleMover.h"
#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<FsmSampleWalk>("FsmSampleWalk")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

// Runs right before the first Enter, so the owner's components are ready to cache.
void FsmSampleWalk::Initialize()
{
    m_Transform = GetTransform();
    m_Mover = GetOwnerObject()->GetComponent<FsmSampleMover>();
}

void FsmSampleWalk::Enter()
{
    CN_LOG_GAME("FSM", "상태 진입 : ", GetName());
}

void FsmSampleWalk::Update(float deltaTime)
{
    if (!m_Transform || !m_Mover)
    {
        return;
    }

    const float direction = GetStateMachine()->GetFloat("MoveInput");
    m_Transform->SetLocalPosition(m_Transform->GetLocalPosition() + Canape::Vector3::Right * (direction * m_Mover->GetWalkSpeed() * deltaTime));
}

void FsmSampleWalk::Exit()
{
    CN_LOG_GAME("FSM", "상태 탈출 : ", GetName());
}
