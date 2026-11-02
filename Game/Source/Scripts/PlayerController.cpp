#include "Scripts/PlayerController.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<PlayerController>("PlayerController")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("moveSpeed", &PlayerController::m_MoveSpeed)
        .property("turnSpeedDegrees", &PlayerController::m_TurnSpeedDegrees);
}

void PlayerController::Awake()
{
}

void PlayerController::Start()
{
}

void PlayerController::Update()
{
    const Canape::Vector3 input = GetMoveInput();
    if (input.IsNearlyZero())
    {
        return;
    }

    Canape::Vector3 cameraForward;
    Canape::Vector3 cameraRight;
    GetCameraAxes(cameraForward, cameraRight);

    const Canape::Vector3 horizontalDirection = cameraForward * input.Z + cameraRight * input.X;
    const Canape::Vector3 direction = (horizontalDirection + Canape::Vector3::Up * input.Y).Normalized();
    const float deltaTime = Canape::Time::DeltaTime();

    Canape::TransformComponent* transform = GetTransform();
    transform->SetLocalPosition(transform->GetLocalPosition() + direction * (m_MoveSpeed * deltaTime));

    if (!horizontalDirection.IsNearlyZero())
    {
        const Canape::Quaternion targetRotation = Canape::Quaternion::LookRotation(horizontalDirection.Normalized());
        const float maxRadians = Canape::Math::ToRadians(m_TurnSpeedDegrees) * deltaTime;
        transform->SetLocalRotation(Canape::Quaternion::RotateTowards(transform->GetLocalRotation(), targetRotation, maxRadians));
    }
}

Canape::Vector3 PlayerController::GetMoveInput() const
{
    const Canape::InputManager& input = Canape::Application::Get().GetInputManager();

    Canape::Vector3 move;
    if (input.IsKeyDown(Canape::Key::Up) || input.IsKeyDown(Canape::Key::W)) move.Z += 1.0f;
    if (input.IsKeyDown(Canape::Key::Down) || input.IsKeyDown(Canape::Key::S)) move.Z -= 1.0f;
    if (input.IsKeyDown(Canape::Key::Right) || input.IsKeyDown(Canape::Key::D)) move.X += 1.0f;
    if (input.IsKeyDown(Canape::Key::Left) || input.IsKeyDown(Canape::Key::A)) move.X -= 1.0f;
    if (input.IsKeyDown(Canape::Key::E)) move.Y += 1.0f;
    if (input.IsKeyDown(Canape::Key::Q)) move.Y -= 1.0f;

    return Canape::Vector3::ClampLength(move, 1.0f);
}

void PlayerController::GetCameraAxes(Canape::Vector3& outForward, Canape::Vector3& outRight) const
{
    outForward = Canape::Vector3::Forward;
    outRight = Canape::Vector3::Right;

    const Canape::CameraComponent* camera = GetOwnerObject()->GetScene()->GetMainCamera();
    if (!camera)
    {
        return;
    }

    const Canape::Vector3 forward = Canape::Vector3::ProjectOnPlane(camera->GetTransform()->GetForward(), Canape::Vector3::Up);
    if (forward.IsNearlyZero())
    {
        return;
    }

    outForward = forward.Normalized();
    outRight = Canape::Vector3::Cross(Canape::Vector3::Up, outForward);
}
