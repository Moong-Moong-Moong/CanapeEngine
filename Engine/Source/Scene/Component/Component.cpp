#include "Scene/Component/Component.h"
#include "Scene/GameObject/GameObject.h"

namespace Canape
{
	TransformComponent* Component::GetTransform() const
	{
		return m_GameObject ? m_GameObject->GetTransform() : nullptr;
	}

	void Component::SetEnabled(bool enabled)
	{
		if (m_Enabled == enabled)
		{
			return;
		}

		m_Enabled = enabled;

		if (m_GameObject)
		{
			m_GameObject->OnComponentEnabledChanged(this);
		}
	}

	bool Component::IsActiveAndEnabled() const
	{
		return m_Enabled && m_GameObject && m_GameObject->IsActiveInHierarchy();
	}
}
