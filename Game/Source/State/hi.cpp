#include "State/hi.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<hi>("hi")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void hi::Enter()
{
}

void hi::Update([[maybe_unused]] float deltaTime)
{
}

void hi::Exit()
{
}
