#pragma once

#include "Scene/Component/Component.h"

namespace Canape
{
	// A collider shapes the RigidBody on its own GameObject, or the nearest ancestor body when it has none.
	class ColliderComponent : public Component
	{
		RTTR_ENABLE(Component)

	protected:
		ColliderComponent() = default;
	};
}
