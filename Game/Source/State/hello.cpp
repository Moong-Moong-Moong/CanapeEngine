#include "State/hello.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<hello>("hello")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void hello::Enter()
{
}

void hello::Update([[maybe_unused]] float deltaTime)
{
}

void hello::Exit()
{
}
