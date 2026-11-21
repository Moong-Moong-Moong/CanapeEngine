#include "Scripts/RippleSource.h"

#include "Simulation/SimulationScene.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<RippleSource>("RippleSource")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("interval", &RippleSource::interval)
        .property("radius", &RippleSource::radius)
        .property("strength", &RippleSource::strength);
}

void RippleSource::Update()
{
    waited += Canape::Time::DeltaTime();
    if(waited < interval) return;

    waited = 0.0f;

    auto* simulation = GetOwnerObject()->GetScene()->GetSimulation();
    if(simulation == nullptr) return;

    simulation->Disturb(GetTransform()->GetWorldPosition(), radius, strength);
}
