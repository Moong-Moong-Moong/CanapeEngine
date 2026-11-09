#pragma once

#include "Scene/Component/Component.h"

namespace Canape
{
	// A collider shapes the RigidBody on its own GameObject, or the nearest ancestor body when it has none.
	class ColliderComponent : public Component
	{
		RTTR_ENABLE(Component)

	public:
		// The body's friction applies to this part unless the override is on.
		bool GetOverrideFriction() const { return m_OverrideFriction; }
		void SetOverrideFriction(bool overrideFriction) { m_OverrideFriction = overrideFriction; }
		float GetFriction() const { return m_Friction; }
		void SetFriction(float friction) { m_Friction = friction; }

	protected:
		ColliderComponent() = default;

	private:
		bool m_OverrideFriction = false;
		float m_Friction = 0.2f;
	};
}
