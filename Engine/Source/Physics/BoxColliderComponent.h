#pragma once

#include "Math/Vector3.h"
#include "Physics/ColliderComponent.h"

namespace Canape
{
	class BoxColliderComponent final : public ColliderComponent
	{
		RTTR_ENABLE(ColliderComponent)

	public:
		BoxColliderComponent() = default;

		// In meters before the Transform scale.
		const Vector3& GetSize() const { return m_Size; }
		void SetSize(const Vector3& size) { m_Size = size; }

	private:
		Vector3 m_Size = Vector3::One;
	};
}
