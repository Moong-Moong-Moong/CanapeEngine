#include "FsamSample/WalkStart.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<WalkStart>("WalkStart")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void WalkStart::Enter()
{
}

void WalkStart::Update([[maybe_unused]] float deltaTime)
{

}

void WalkStart::Exit()
{
}
