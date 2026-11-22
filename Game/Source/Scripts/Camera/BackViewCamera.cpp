#include "Scripts/Camera/BackViewCamera.h"

#include <cmath>

#include "FSM/StateMachine.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<BackViewCamera>("BackViewCamera")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("target", &BackViewCamera::m_Target)
        .property("distance", &BackViewCamera::m_Distance)
        .property("pivotHeight", &BackViewCamera::m_PivotHeight)
        .property("sensitivity", &BackViewCamera::m_Sensitivity)
        .property("minPitch", &BackViewCamera::m_MinPitch)
        .property("maxPitch", &BackViewCamera::m_MaxPitch)
        .property("defaultPitch", &BackViewCamera::m_DefaultPitch)
        .property("recenterDelay", &BackViewCamera::m_RecenterDelay)
        .property("recenterSpeed", &BackViewCamera::m_RecenterSpeed)
        .property("minHeightAboveFeet", &BackViewCamera::m_MinHeightAboveFeet)
        .property("followSharpness", &BackViewCamera::m_FollowSharpness)
        .property("movingSpeed", &BackViewCamera::m_MovingSpeed)
        .property("yaw", &BackViewCamera::m_Yaw)
        .property("pitch", &BackViewCamera::m_Pitch);
}

namespace
{
    // Fraction of the remaining gap to close this frame, independent of frame rate.
    float Smoothing(float sharpness, float deltaTime)
    {
        return sharpness > 0.0f ? 1.0f - std::exp(-sharpness * deltaTime) : 1.0f;
    }
}

void BackViewCamera::Start()
{
    const Canape::GameObject* target = GetGameObject(m_Target);
    if (!target)
    {
        return;
    }

    // Begin behind the target.
    m_Yaw = TargetYaw(*target);
    m_Pitch = m_DefaultPitch;
    m_Pivot = target->GetTransform()->GetWorldPosition() + Canape::Vector3::Up * m_PivotHeight;
    m_HasPivot = true;
    Place(m_Pivot);
}

void BackViewCamera::Update()
{
    const Canape::GameObject* target = GetGameObject(m_Target);
    if (!target)
    {
        return;
    }

    const float deltaTime = Canape::Time::DeltaTime();
    const Canape::IntVector2 mouseDelta = Canape::Application::Get().GetInputManager().GetMouseDelta();

    //***** Mouse orbit: turns only the camera, never the target.
    if (mouseDelta.X != 0 || mouseDelta.Y != 0)
    {
        m_Yaw = Canape::Math::WrapDegrees(m_Yaw + mouseDelta.X * m_Sensitivity);
        m_Pitch = Canape::Math::Clamp(m_Pitch + mouseDelta.Y * m_Sensitivity, m_MinPitch, m_MaxPitch);
        m_IdleTime = 0.0f;
        m_Recentering = false;
        m_AttackRecenter = false;
    }
    else
    {
        m_IdleTime += deltaTime;
        if (m_IdleTime >= m_RecenterDelay)
        {
            m_Recentering = true;
        }
    }

    //***** Movement is camera-relative, so swinging behind a target that walks sideways would turn its
    //      path into a circle. While it moves, an idle swing pauses (an attack swing still runs).
    const Canape::Vector3 feet = target->GetTransform()->GetWorldPosition();
    const Canape::Vector3 step(feet.X - m_LastFeet.X, 0.0f, feet.Z - m_LastFeet.Z);
    const bool moving = m_HasLastFeet && deltaTime > 0.0f && step.Length() / deltaTime > m_MovingSpeed;
    m_LastFeet = feet;
    m_HasLastFeet = true;
    if (moving)
    {
        m_IdleTime = 0.0f;
        if (!m_AttackRecenter)
        {
            m_Recentering = false;
        }
    }

    //***** An attack swings the camera back behind the target.
    const bool attacking = IsTargetAttacking(*target);
    if (attacking && !m_WasAttacking)
    {
        m_Recentering = true;
        m_AttackRecenter = true;
    }
    m_WasAttacking = attacking;

    //***** Swing back behind the target, easing in. Mouse input above cancels it.
    if (m_Recentering)
    {
        const float blend = Smoothing(m_RecenterSpeed, deltaTime);
        m_Yaw = Canape::Math::WrapDegrees(m_Yaw + Canape::Math::WrapDegrees(TargetYaw(*target) - m_Yaw) * blend);
        m_Pitch += (m_DefaultPitch - m_Pitch) * blend;
    }
    else
    {
        m_AttackRecenter = false;
    }
}

void BackViewCamera::LateUpdate()
{
    const Canape::GameObject* target = GetGameObject(m_Target);
    if (!target)
    {
        return;
    }

    const Canape::Vector3 feet = target->GetTransform()->GetWorldPosition();
    const Canape::Vector3 desiredPivot = feet + Canape::Vector3::Up * m_PivotHeight;
    if (!m_HasPivot)
    {
        m_Pivot = desiredPivot;
        m_HasPivot = true;
    }
    m_Pivot = Canape::Lerp(m_Pivot, desiredPivot, Smoothing(m_FollowSharpness, Canape::Time::DeltaTime()));

    //***** Vertical limit: the configured range, and never below the target's feet.
    const float lowest = Canape::Math::Clamp(GroundPitchLimit(feet, m_Pivot), m_MinPitch, m_MaxPitch);
    m_Pitch = Canape::Math::Clamp(m_Pitch, lowest, m_MaxPitch);

    Place(m_Pivot);
}

float BackViewCamera::TargetYaw(const Canape::GameObject& target) const
{
    const Canape::Vector3 forward = target.GetTransform()->GetForward();
    if (std::abs(forward.X) < 1e-5f && std::abs(forward.Z) < 1e-5f)
    {
        return m_Yaw;
    }
    // Rotator yaw turns +Z toward +X.
    return Canape::Math::ToDegrees(std::atan2(forward.X, forward.Z));
}

bool BackViewCamera::IsTargetAttacking(const Canape::GameObject& target) const
{
    const Canape::StateMachine* stateMachine = target.GetComponent<Canape::StateMachine>();
    return stateMachine && stateMachine->IsStateActive("AttackState");
}

float BackViewCamera::GroundPitchLimit(const Canape::Vector3& feet, const Canape::Vector3& pivot) const
{
    if (m_Distance <= 0.0f)
    {
        return m_MinPitch;
    }
    // The camera sits at pivot - forward * distance, and forward.Y = -sin(pitch),
    // so its height is pivot.Y + sin(pitch) * distance.
    const float sine = Canape::Math::Clamp((feet.Y + m_MinHeightAboveFeet - pivot.Y) / m_Distance, -1.0f, 1.0f);
    return Canape::Math::ToDegrees(std::asin(sine));
}

void BackViewCamera::Place(const Canape::Vector3& pivot)
{
    const Canape::Quaternion rotation = Canape::Rotator(m_Pitch, m_Yaw, 0.0f).ToQuaternion();

    Canape::TransformComponent* transform = GetTransform();
    transform->SetPosition(pivot - rotation.GetForward() * m_Distance);
    transform->SetRotation(rotation);
}
