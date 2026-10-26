#include "Scene/Component/ComponentRegistry.h"
#include "Scene/Component/RenderableComponent.h"
#include "Scene/Component/ScriptComponent.h"

#include <algorithm>
#include <vector>

namespace Canape
{
	void ComponentRegistry::Register(Component* component)
	{
		if (!component)
		{
			return;
		}

		m_pendingAdd.push_back(component);
	}

	void ComponentRegistry::Unregister(Component* component)
	{
		if (!component)
		{
			return;
		}

		m_pendingRemove.push_back(component);
	}

	void ComponentRegistry::FlushPending()
	{
		//***** 추가
		for (Component* component : m_pendingAdd)
		{
			// 추가와 삭제가 동시에 이루어진다면 추가하지 않음
			if (component->IsPendingDestroy())
			{
				continue;
			}

			//*** ScriptComponent
			if (ScriptComponent* script = dynamic_cast<ScriptComponent*>(component))
			{
				m_scripts.push_back(script);
			}

			//*** RenderableComponent
			if (RenderableComponent* renderable = dynamic_cast<RenderableComponent*>(component))
			{
				m_renderables.push_back(renderable);
			}
		}
		m_pendingAdd.clear();

		//***** 삭제
		for (Component* component : m_pendingRemove)
		{
			//*** ScriptComponent
			if (ScriptComponent* script = dynamic_cast<ScriptComponent*>(component))
			{
				std::erase(m_scripts, script);
			}

			//*** RenderableComponent
			if (RenderableComponent* renderable = dynamic_cast<RenderableComponent*>(component))
			{
				std::erase(m_renderables, renderable);
			}
		}
		m_pendingRemove.clear();
	}
}
