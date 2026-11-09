#include "State/haha.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<haha>("haha")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void haha::Enter()
{
}

void haha::Update([[maybe_unused]] float deltaTime)
{
}

void haha::Exit()
{
}
