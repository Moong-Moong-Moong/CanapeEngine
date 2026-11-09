#include "FsmSample/FsmSampleRun.h"
#include "FsmSample/FsmSampleMover.h"
#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<FsmSampleRun>("FsmSampleRun")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void FsmSampleRun::Initialize()
{
    m_Transform = GetTransform();
    m_Mover = GetOwnerObject()->GetComponent<FsmSampleMover>();
}

void FsmSampleRun::Enter()
{
    CN_LOG_GAME("FSM", "상태 진입 : ", GetName());
}

void FsmSampleRun::Update(float deltaTime)
{
    if (!m_Transform || !m_Mover)
    {
        return;
    }

    const float direction = GetStateMachine()->GetFloat("MoveInput");
    m_Transform->SetLocalPosition(m_Transform->GetLocalPosition() + Canape::Vector3::Right * (direction * m_Mover->GetRunSpeed() * deltaTime));
}

void FsmSampleRun::Exit()
{
    CN_LOG_GAME("FSM", "상태 탈출 : ", GetName());
}
