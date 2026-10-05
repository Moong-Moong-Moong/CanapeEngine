#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/TransformComponent.h"

namespace Canape
{
	Matrix4 CameraComponent::GetViewMatrix() const
	{
		const TransformComponent* transform = GetTransform();
		const Quaternion rotation = transform->GetRotation();
		return Matrix4::LookTo(transform->GetPosition(), rotation.GetForward(), rotation.GetUp());
	}

	Matrix4 CameraComponent::GetProjectionMatrix(float aspectRatio) const
	{
		return Matrix4::Perspective(Math::ToRadians(m_FieldOfView), aspectRatio, m_NearClip, m_FarClip);
	}

	Matrix4 CameraComponent::GetViewProjectionMatrix(float aspectRatio) const
	{
		return GetViewMatrix() * GetProjectionMatrix(aspectRatio);
	}
}
