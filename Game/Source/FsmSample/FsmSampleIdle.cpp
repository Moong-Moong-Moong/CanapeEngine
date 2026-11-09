#include "FsmSample/FsmSampleIdle.h"
#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<FsmSampleIdle>("FsmSampleIdle")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void FsmSampleIdle::Enter()
{
    CN_LOG_GAME("FSM", "상태 진입 : ", GetName());
}

void FsmSampleIdle::Update([[maybe_unused]] float deltaTime)
{
}

void FsmSampleIdle::Exit()
{
    CN_LOG_GAME("FSM", "상태 탈출 : ", GetName());
}
