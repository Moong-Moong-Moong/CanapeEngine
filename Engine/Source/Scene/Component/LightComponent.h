#pragma once

#include "Scene/Component/Component.h"
#include "DX11Renderer/Scene/RenderScene.h"
#include "Math/Vector3.h"

#include <cstdint>

namespace Canape
{
	class LightComponent final : public Component
	{
	public:
		LightComponent();

		LightType GetType() const { return m_Type; }
		void SetType(LightType type) { m_Type = type; }

		const Vector3& GetColor() const { return m_Color; }
		void SetColor(const Vector3& color) { m_Color = color; }

		float GetIntensity() const { return m_Intensity; }
		void SetIntensity(float intensity) { m_Intensity = intensity; }

		float GetRange() const { return m_Range; }
		void SetRange(float range) { m_Range = range; }

		float GetInnerAngle() const { return m_InnerAngleRadians; }
		void SetInnerAngle(float radians) { m_InnerAngleRadians = radians; }

		float GetOuterAngle() const { return m_OuterAngleRadians; }
		void SetOuterAngle(float radians) { m_OuterAngleRadians = radians; }

		bool GetCastShadow() const { return m_CastShadow; }
		void SetCastShadow(bool castShadow) { m_CastShadow = castShadow; }

		RenderLight BuildRenderLight() const;

	private:
		uint64_t m_Id = 0;
		LightType m_Type = LightType::Point;
		Vector3 m_Color{ 1.0f, 1.0f, 1.0f };
		float m_Intensity = 1.0f;
		float m_Range = 10.0f;
		float m_InnerAngleRadians = 0.35f;
		float m_OuterAngleRadians = 0.52f;
		bool m_CastShadow = false;
	};
}
