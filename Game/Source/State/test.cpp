#include "State/test.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<test>("test")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void test::Enter()
{
}

void test::Update([[maybe_unused]] float deltaTime)
{
}

void test::Exit()
{
}
