#include "Scripts/FlyCamera.h"

#include <cmath>

RTTR_REGISTRATION
{
    rttr::registration::class_<FlyCamera>("FlyCamera")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("moveSpeed", &FlyCamera::m_MoveSpeed)
        .property("lookSensitivity", &FlyCamera::m_LookSensitivity)
        .property("resetKey", &FlyCamera::m_ResetKey);
}

namespace
{
    constexpr float PitchLimitDegrees = 89.4f;
    constexpr float WheelNotchFactor = 1.25f;
    constexpr float MinSpeedScale = 1.0f / 8.0f;
    constexpr float MaxSpeedScale = 8.0f;

    Canape::Rotator toYawPitch(const Canape::Quaternion& rotation)
    {
        using namespace Canape;

        const auto forward = rotation.GetForward();
        const auto yaw = Math::ToDegrees(std::atan2(forward.X, forward.Z));
        const auto pitch = Math::ToDegrees(std::asin(Math::Clamp(-forward.Y, -1.0f, 1.0f)));

        return Rotator(pitch, yaw, 0.0f);
    }
}

void FlyCamera::ReturnHome()
{
    constexpr Canape::Vector3 homePosition{ 0.0f, 7.3f, -3.9f };
    constexpr float homeYaw = 0.0f;
    constexpr float homePitch = Canape::Math::ToDegrees(0.873f);

    ApplyPose(homePosition, homeYaw, homePitch);
}

void FlyCamera::Start()
{
    ReturnHome();
}

void FlyCamera::Update()
{
    using namespace Canape;
    using enum Canape::Key;

    const InputManager& input = Application::Get().GetInputManager();
    const auto deltaTime = Time::DeltaTime();

    if (input.IsKeyPressed(m_ResetKey))
    {
        ReturnHome();
    }

    const TransformComponent& transform = *GetTransform();
    auto position = transform.GetLocalPosition();
    const auto look = toYawPitch(transform.GetLocalRotation());
    auto yaw = look.Yaw;
    auto pitch = look.Pitch;

    if (input.IsMouseDown(MouseButton::Right))
    {
        const IntVector2 mouseDelta = input.GetMouseDelta();
        yaw = Math::WrapDegrees(yaw + mouseDelta.X * m_LookSensitivity);
        pitch = Math::Clamp(
            pitch + mouseDelta.Y * m_LookSensitivity,
            -PitchLimitDegrees,
            PitchLimitDegrees
        );
    }

    const float wheel = input.GetMouseWheel();
    if (wheel != 0.0f)
    {
        m_SpeedScale = Math::Clamp(
            m_SpeedScale * std::pow(WheelNotchFactor, wheel),
            MinSpeedScale,
            MaxSpeedScale
        );
    }

    const auto rotation = Rotator(pitch, yaw, 0.0f).ToQuaternion();

    const float right =
        (input.IsKeyDown(D) ? 1.0f : 0.0f) -
        (input.IsKeyDown(A) ? 1.0f : 0.0f);
    const float up =
        (input.IsKeyDown(Space) ? 1.0f : 0.0f) -
        (input.IsKeyDown(Shift) ? 1.0f : 0.0f);
    const float forward =
        (input.IsKeyDown(W) ? 1.0f : 0.0f) -
        (input.IsKeyDown(S) ? 1.0f : 0.0f);

    const Vector3 wish =
        rotation.GetRight() * right +
        Vector3::Up * up +
        rotation.GetForward() * forward;
    if (!wish.IsNearlyZero())
    {
        auto scaledSpeed = m_MoveSpeed * m_SpeedScale * deltaTime;
        position += wish.Normalized() * scaledSpeed;
    }

    ApplyPose(position, yaw, pitch);
}

void FlyCamera::ApplyPose(const Canape::Vector3& position, float yaw, float pitch) const
{
    using namespace Canape;

    auto& transform = *GetTransform();

    transform.SetLocalPosition(position);
    transform.SetLocalRotation(
        Rotator(pitch, yaw, 0.0f).ToQuaternion()
    );
}
