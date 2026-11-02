#pragma once

#include "Scene/Component/Component.h"
#include "DX11Renderer/Scene/RenderScene.h"
#include "Math/Vector3.h"

namespace Canape
{
	class FogComponent final : public Component
	{
		RTTR_ENABLE(Component)

	public:
		FogComponent() = default;

		FogMode GetMode() const { return m_Mode; }
		void SetMode(FogMode mode) { m_Mode = mode; }

		const Vector3& GetColor() const { return m_Color; }
		void SetColor(const Vector3& color) { m_Color = color; }

		float GetStartDistance() const { return m_StartDistance; }
		void SetStartDistance(float distance) { m_StartDistance = distance; }

		float GetEndDistance() const { return m_EndDistance; }
		void SetEndDistance(float distance) { m_EndDistance = distance; }

		float GetDensity() const { return m_Density; }
		void SetDensity(float density) { m_Density = density; }

		RenderFog BuildRenderFog() const;

	private:
		FogMode m_Mode = FogMode::Linear;
		Vector3 m_Color{ 0.5f, 0.5f, 0.5f };
		float m_StartDistance = 0.0f;
		float m_EndDistance = 100.0f;
		float m_Density = 0.01f;
	};
}
