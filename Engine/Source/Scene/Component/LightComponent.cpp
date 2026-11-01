#include "Scene/Component/LightComponent.h"
#include "Scene/Component/TransformComponent.h"

namespace Canape
{
	namespace
	{
		uint64_t s_NextLightId = 0;
	}

	LightComponent::LightComponent()
		: m_Id(++s_NextLightId)
	{
	}

	RenderLight LightComponent::BuildRenderLight() const
	{
		const TransformComponent* transform = GetTransform();

		RenderLight light;
		light.id = m_Id;
		light.type = m_Type;
		light.color = m_Color;
		light.intensity = m_Intensity;
		light.position = transform->GetPosition();
		light.direction = transform->GetForward();
		light.range = m_Range;
		light.innerAngleRadians = m_InnerAngleRadians;
		light.outerAngleRadians = m_OuterAngleRadians;
		light.castShadow = m_CastShadow;
		return light;
	}
}
