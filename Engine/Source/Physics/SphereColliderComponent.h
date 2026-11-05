#pragma once

#include "Physics/ColliderComponent.h"

namespace Canape
{
	class SphereColliderComponent final : public ColliderComponent
	{
		RTTR_ENABLE(ColliderComponent)

	public:
		SphereColliderComponent() = default;

		// In meters before the Transform scale.
		float GetRadius() const { return m_Radius; }
		void SetRadius(float radius) { m_Radius = radius; }

	private:
		float m_Radius = 0.5f;
	};
}
