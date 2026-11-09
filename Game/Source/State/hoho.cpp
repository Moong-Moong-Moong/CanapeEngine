#include "State/hoho.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<hoho>("hoho")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void hoho::Enter()
{
}

void hoho::Update([[maybe_unused]] float deltaTime)
{
}

void hoho::Exit()
{
}
