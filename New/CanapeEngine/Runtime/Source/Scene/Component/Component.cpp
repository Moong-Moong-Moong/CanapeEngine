#include "Scene/Component/Component.h"
#include "Scene/GameObject/GameObject.h"

namespace Canape
{
	TransformComponent* Component::GetTransform() const
	{
		return m_GameObject ? m_GameObject->GetTransform() : nullptr;
	}
}
