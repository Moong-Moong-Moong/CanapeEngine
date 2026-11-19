#include "MoveSample/PlayerCamera.h"

#include <cmath>

RTTR_REGISTRATION
{
    rttr::registration::class_<PlayerCamera>("PlayerCamera")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("target", &PlayerCamera::m_Target)
        .property("distance", &PlayerCamera::m_Distance)
        .property("height", &PlayerCamera::m_Height)
        .property("lookHeight", &PlayerCamera::m_LookHeight)
        .property("followSpeed", &PlayerCamera::m_FollowSpeed);
}

void PlayerCamera::Start()
{
    Follow(1.0f);
}

void PlayerCamera::LateUpdate()
{
    const float blend = m_FollowSpeed > 0.0f ? 1.0f - std::exp(-m_FollowSpeed * Canape::Time::DeltaTime()) : 1.0f;
    Follow(blend);
}

void PlayerCamera::Follow(float blend)
{
    const Canape::GameObject* target = GetGameObject(m_Target);
    if (!target)
    {
        return;
    }

    const Canape::Vector3 targetPosition = target->GetTransform()->GetWorldPosition();
    const Canape::Vector3 desired = targetPosition - Canape::Vector3::Forward * m_Distance + Canape::Vector3::Up * m_Height;

    Canape::TransformComponent* transform = GetTransform();
    const Canape::Vector3 position = Canape::Lerp(transform->GetWorldPosition(), desired, blend);
    transform->SetPosition(position);

    const Canape::Vector3 lookAt = targetPosition + Canape::Vector3::Up * m_LookHeight;
    const Canape::Vector3 direction = lookAt - position;
    if (!direction.IsNearlyZero())
    {
        transform->SetRotation(Canape::Quaternion::LookRotation(direction.Normalized()));
    }
}
