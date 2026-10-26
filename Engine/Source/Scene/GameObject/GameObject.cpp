#include "Scene/GameObject/GameObject.h"
#include "../Component/Component.h"
#include "Scene/Scene.h"
#include "Core/Containers/SlotMap.h"

namespace Canape
{
	GameObject::GameObject()
	{
		auto transform = std::unique_ptr<TransformComponent>(new TransformComponent());
		m_Transform = transform.get();
		transform->m_GameObject = this;
		m_Components.push_back(std::move(transform));
	}

	bool GameObject::RemoveComponent(Component* component)
	{
		if (!component || component == m_Transform || component->m_GameObject != this || component->m_PendingDestroy)
		{
			return false;
		}

		component->m_PendingDestroy = true;
		if (m_Scene)
		{
			m_Scene->UnregisterComponent(component);
		}
		return true;
	}

	void GameObject::RegisterComponent(Component* component)
	{
		if (!m_Scene || m_PendingDestroy)
		{
			return;
		}

		m_Scene->RegisterComponent(component);
	}

	void GameObject::RemoveAllComponents()
	{
		for (const std::unique_ptr<Component>& component : m_Components)
		{
			if (component.get() == m_Transform || component->m_PendingDestroy)
			{
				continue;
			}

			component->m_PendingDestroy = true;
			if (m_Scene)
			{
				m_Scene->UnregisterComponent(component.get());
			}
		}
	}

	void GameObject::DestroyComponent(Component* component)
	{
		for (auto it = m_Components.begin(); it != m_Components.end(); ++it)
		{
			if (it->get() == component)
			{
				m_Components.erase(it);
				return;
			}
		}
	}
}
