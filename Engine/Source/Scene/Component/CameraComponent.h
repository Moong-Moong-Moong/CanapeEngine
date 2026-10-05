#pragma once

#include "Scene/Component/Component.h"
#include "Math/Matrix4.h"

namespace Canape
{
	class CameraComponent final : public Component
	{
	public:
		CameraComponent() = default;

		float GetFieldOfView() const { return m_FieldOfView; }
		void SetFieldOfView(float degrees) { m_FieldOfView = degrees; }

		float GetNearClip() const { return m_NearClip; }
		void SetNearClip(float nearClip) { m_NearClip = nearClip; }

		float GetFarClip() const { return m_FarClip; }
		void SetFarClip(float farClip) { m_FarClip = farClip; }

		Matrix4 GetViewMatrix() const;
		Matrix4 GetProjectionMatrix(float aspectRatio) const;
		Matrix4 GetViewProjectionMatrix(float aspectRatio) const;

	private:
		float m_FieldOfView = 60.0f;
		float m_NearClip = 0.1f;
		float m_FarClip = 1000.0f;
	};
}
