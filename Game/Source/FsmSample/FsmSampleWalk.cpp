#include "FsmSample/FsmSampleWalk.h"
#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<FsmSampleWalk>("FsmSampleWalk")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void FsmSampleWalk::Enter()
{
    CN_LOG_GAME("FSM", "상태 진입 : ", GetName());
}

void FsmSampleWalk::Update([[maybe_unused]] float deltaTime)
{
}

void FsmSampleWalk::Exit()
{
    CN_LOG_GAME("FSM", "상태 탈출 : ", GetName());
}
