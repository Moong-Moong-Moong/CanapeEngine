#include "FsmSample/FsmSampleRun.h"
#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<FsmSampleRun>("FsmSampleRun")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void FsmSampleRun::Enter()
{
    CN_LOG_GAME("FSM", "상태 진입 : ", GetName());
}

void FsmSampleRun::Update([[maybe_unused]] float deltaTime)
{
}

void FsmSampleRun::Exit()
{
    CN_LOG_GAME("FSM", "상태 탈출 : ", GetName());
}
