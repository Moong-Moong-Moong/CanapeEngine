#include "Scripts/Test.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<Test>("Test")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void Test::Awake()
{
}

void Test::Start()
{
}

void Test::FixedUpdate()
{
}

void Test::Update()
{
}
