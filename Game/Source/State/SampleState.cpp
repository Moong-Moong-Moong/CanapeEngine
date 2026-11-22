#include "State/SampleState.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<SampleState>("SampleState")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void SampleState::Initialize()
{
}

void SampleState::Enter()
{
}

void SampleState::Update([[maybe_unused]] float deltaTime)
{
}

void SampleState::Exit()
{
}
