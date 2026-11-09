#include "State/Grounded.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<Grounded>("Grounded")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void Grounded::Enter()
{
}

void Grounded::Update([[maybe_unused]] float deltaTime)
{
}

void Grounded::Exit()
{
}
