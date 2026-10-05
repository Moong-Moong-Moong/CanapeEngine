#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/MonoBehaviour.h"

#include <algorithm>

namespace Canape
{
	GameObject::GameObject(Scene* scene, std::string name, GameObjectHandle handle)
		: m_Name(std::move(name))
		, m_Handle(handle)
		, m_Scene(scene)
	{
		auto transform = std::unique_ptr<TransformComponent>(new TransformComponent());
		m_Transform = transform.get();
		AttachComponent(std::move(transform));
	}

	GameObject::~GameObject()
	{
		for (std::size_t i = 0; i < m_Behaviours.size(); ++i)
		{
			DestroyBehaviour(m_Behaviours[i]);
		}
		m_Behaviours.clear();
		m_Components.clear();
	}

	bool GameObject::IsActiveInHierarchy() const
	{
		if (!m_Active)
		{
			return false;
		}

		const TransformComponent* parent = m_Transform->GetParent();
		return parent ? parent->GetGameObject()->IsActiveInHierarchy() : true;
	}

	void GameObject::SetActive(bool active)
	{
		if (m_Active == active)
		{
			return;
		}

		m_Active = active;
		RefreshHierarchy();
	}

	bool GameObject::RemoveComponent(Component* component)
	{
		if (!component || component == m_Transform || component->m_GameObject != this || component->m_PendingDestroy)
		{
			return false;
		}

		component->m_PendingDestroy = true;
		return true;
	}

	void GameObject::AttachComponent(std::unique_ptr<Component> component)
	{
		component->m_GameObject = this;
		Component* attached = component.get();
		m_Components.push_back(std::move(component));

		if (auto* behaviour = dynamic_cast<MonoBehaviour*>(attached))
		{
			m_Behaviours.push_back(behaviour);
			SyncBehaviour(behaviour);
		}
	}

	void GameObject::Update()
	{
		for (std::size_t i = 0; i < m_Behaviours.size(); ++i)
		{
			MonoBehaviour* behaviour = m_Behaviours[i];
			if (!behaviour->m_EnabledState || behaviour->m_PendingDestroy)
			{
				continue;
			}

			if (!behaviour->m_Started)
			{
				behaviour->m_Started = true;
				behaviour->Start();

				if (!behaviour->m_EnabledState || behaviour->m_PendingDestroy)
				{
					continue;
				}
			}

			behaviour->Update();
		}
	}

	void GameObject::LateUpdate()
	{
		for (std::size_t i = 0; i < m_Behaviours.size(); ++i)
		{
			MonoBehaviour* behaviour = m_Behaviours[i];
			if (!behaviour->m_EnabledState || behaviour->m_PendingDestroy || !behaviour->m_Started)
			{
				continue;
			}

			behaviour->LateUpdate();
		}
	}

	void GameObject::FlushDestroyedComponents()
	{
		for (std::size_t i = 0; i < m_Behaviours.size(); ++i)
		{
			if (m_Behaviours[i]->m_PendingDestroy)
			{
				DestroyBehaviour(m_Behaviours[i]);
			}
		}

		std::erase_if(m_Behaviours, [](const MonoBehaviour* behaviour)
		{
			return behaviour->m_PendingDestroy;
		});

		std::erase_if(m_Components, [](const std::unique_ptr<Component>& component)
		{
			return component->m_PendingDestroy;
		});
	}

	void GameObject::OnComponentEnabledChanged(Component* component)
	{
		if (auto* behaviour = dynamic_cast<MonoBehaviour*>(component))
		{
			SyncBehaviour(behaviour);
		}
	}

	void GameObject::RefreshHierarchy()
	{
		for (std::size_t i = 0; i < m_Behaviours.size(); ++i)
		{
			SyncBehaviour(m_Behaviours[i]);
		}

		const std::vector<TransformComponent*> children = m_Transform->GetChildren();
		for (TransformComponent* child : children)
		{
			child->GetGameObject()->RefreshHierarchy();
		}
	}

	void GameObject::SyncBehaviour(MonoBehaviour* behaviour)
	{
		if (behaviour->m_PendingDestroy)
		{
			return;
		}

		if (!behaviour->m_Awoken && behaviour->GetGameObject()->IsActiveInHierarchy())
		{
			behaviour->m_Awoken = true;
			behaviour->Awake();
		}

		const bool shouldEnable = behaviour->m_Awoken && !behaviour->m_PendingDestroy && behaviour->IsActiveAndEnabled();
		if (shouldEnable == behaviour->m_EnabledState)
		{
			return;
		}

		behaviour->m_EnabledState = shouldEnable;
		if (shouldEnable)
		{
			behaviour->OnEnable();
		}
		else
		{
			behaviour->OnDisable();
		}
	}

	void GameObject::DestroyBehaviour(MonoBehaviour* behaviour)
	{
		if (behaviour->m_EnabledState)
		{
			behaviour->m_EnabledState = false;
			behaviour->OnDisable();
		}

		if (behaviour->m_Awoken)
		{
			behaviour->m_Awoken = false;
			behaviour->OnDestroy();
		}
	}
}
