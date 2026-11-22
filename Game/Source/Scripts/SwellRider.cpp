#include "Scripts/SwellRider.h"

#include <algorithm>

#include "Simulation/SimulationScene.h"

namespace
{
    // The lean divides by the footprint.
    constexpr float MinFootprintMeters = 0.01f;
}

RTTR_REGISTRATION
{
    rttr::registration::class_<SwellRider>("SwellRider")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("lengthMeters", &SwellRider::GetLengthMeters, &SwellRider::SetLengthMeters)
        .property("widthMeters", &SwellRider::GetWidthMeters, &SwellRider::SetWidthMeters);
}

void SwellRider::SetLengthMeters(float meters)
{
    lengthMeters = std::max(meters, MinFootprintMeters);
}

void SwellRider::SetWidthMeters(float meters)
{
    widthMeters = std::max(meters, MinFootprintMeters);
}

void SwellRider::Start()
{
    restHeight = GetTransform()->GetPosition().Y;

    const auto forward = GetTransform()->GetForward();
    heading = Canape::Vector3{ forward.X, 0.0f, forward.Z }.Normalized().XZ();
}

void SwellRider::Update()
{
    using namespace Canape;

    auto* simulation = GetOwnerObject()->GetScene()->GetSimulation();
    if(simulation == nullptr) return;

    auto& transform = *GetTransform();
    const auto center = transform.GetWorldPosition().XZ();
    const auto toBow = heading * (lengthMeters * 0.5f);
    const auto toStarboard = Vector2{ heading.Y, -heading.X } * (widthMeters * 0.5f);

    const auto bow = simulation->SwellHeightAt(center + toBow);
    const auto stern = simulation->SwellHeightAt(center - toBow);
    const auto starboard = simulation->SwellHeightAt(center + toStarboard);
    const auto port = simulation->SwellHeightAt(center - toStarboard);

    // Stern to bow and port to starboard, both lying on the water.
    const Vector3 keel
    {
        toBow.X * 2.0f,
        bow - stern,
        toBow.Y * 2.0f
    };
    const Vector3 beam
    {
        toStarboard.X * 2.0f,
        starboard - port,
        toStarboard.Y * 2.0f
    };

    auto position = transform.GetPosition();
    position.Y = restHeight + (bow + stern + starboard + port) * 0.25f;
    transform.SetPosition(position);
    transform.SetRotation(Quaternion::LookRotation(keel, Vector3::Cross(keel, beam).Normalized()));
}
