#include "Scene/Component/FogComponent.h"

namespace Canape
{
	RenderFog FogComponent::BuildRenderFog() const
	{
		RenderFog fog;
		fog.mode = m_Mode;
		fog.color = m_Color;
		fog.startDistance = m_StartDistance;
		fog.endDistance = m_EndDistance;
		fog.density = m_Density;
		return fog;
	}
}
