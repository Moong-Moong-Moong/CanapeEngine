#include "Scripts/CameraController.h"

void CameraController::Awake()
{
}

void CameraController::Start()
{
}

void CameraController::Update()
{
    const Canape::IntVector2 mouseDelta = Canape::Application::Get().GetInputManager().GetMouseDelta();

    m_Yaw = Canape::Math::WrapDegrees(m_Yaw + mouseDelta.X * m_Sensitivity);
    m_Pitch = Canape::Math::Clamp(m_Pitch + mouseDelta.Y * m_Sensitivity, m_MinPitch, m_MaxPitch);
}

void CameraController::LateUpdate()
{
    const Canape::GameObject* target = Resolve(m_Target);
    if (!target)
    {
        return;
    }

    const Canape::Quaternion rotation = Canape::Rotator(m_Pitch, m_Yaw, 0.0f).ToQuaternion();
    const Canape::Vector3 pivot = target->GetTransform()->GetPosition() + Canape::Vector3::Up * m_PivotHeight;

    Canape::TransformComponent* transform = GetTransform();
    transform->SetLocalPosition(pivot - rotation.GetForward() * m_Distance);
    transform->SetLocalRotation(rotation);
}
