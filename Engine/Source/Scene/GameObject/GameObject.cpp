#include "Scene/GameObject/GameObject.h"
#include "../Component/Component.h"
#include "Scene/Component/RenderableComponent.h"
#include "Scene/Component/ScriptComponent.h"
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

	GameObject::GameObject(Scene* scene, std::string name, Handle<GameObject> handle)
		: GameObject()
	{
		m_Name = std::move(name);
		m_Scene = scene;
		m_handle = handle;
	}

	bool GameObject::RemoveComponent(Component* component)
	{
		if (!component || component == m_Transform || component->m_GameObject != this || component->m_PendingDestroy)
		{
			return false;
		}

		component->m_PendingDestroy = true;
		m_PendingRemoveComponents.push_back(component);
		return true;
	}

	void GameObject::BeginUpdate()
	{
		//*** 삭제 예정 컴포넌트 삭제
		FlushRemovedComponents();
		//*** 추가 예정 컴포넌트 활성화
		AddPendingComponents();
	}

	void GameObject::FlushRemovedComponents()
	{
		for (Component* removedComponent : m_PendingRemoveComponents)
		{
			//*** Script Component 목록에서 삭제
			auto scriptComponent = m_ScriptComponents.begin();
			while (scriptComponent != m_ScriptComponents.end())
			{
				if (*scriptComponent == removedComponent)
				{
					static_cast<ScriptComponent*>(removedComponent)->OnDestroy();
					m_ScriptComponents.erase(scriptComponent);
					break;
				}

				++scriptComponent;
			}

			//*** Renderable Component 목록에서 삭제
			auto renderableComponent = m_RenderableComponents.begin();
			while (renderableComponent != m_RenderableComponents.end())
			{
				if (*renderableComponent == removedComponent)
				{
					m_RenderableComponents.erase(renderableComponent);
					break;
				}

				++renderableComponent;
			}

			//*** Component 목록에서 삭제
			auto component = m_Components.begin();
			while (component != m_Components.end())
			{
				if (component->get() == removedComponent)
				{
					m_Components.erase(component);
					break;
				}

				++component;
			}
		}

		m_PendingRemoveComponents.clear();
	}

	void GameObject::AddPendingComponents()
	{
		for (const std::unique_ptr<Component>& component : m_Components)
		{

			if (component->m_PendingAdd)
			{
				component->m_PendingAdd = false;

				//***** Script Component 목록에 추가
				ScriptComponent* scriptComponent = dynamic_cast<ScriptComponent*>(component.get());
				if (scriptComponent && component->m_Enabled)
				{
					m_ScriptComponents.push_back(scriptComponent);
				}

				//***** Renderable Component 목록에 추가
				RenderableComponent* renderableComponent = dynamic_cast<RenderableComponent*>(component.get());
				if (renderableComponent && component->m_Enabled)
				{
					m_RenderableComponents.push_back(renderableComponent);
				}
			}
		}
	}

	void GameObject::Update()
	{
		if (m_Active == false)
			return;

		BeginUpdate();
		UpdateScriptComponents();
	}

	void GameObject::UpdateScriptComponents()
	{
		for (ScriptComponent* scriptComp : m_ScriptComponents)
		{
			if (scriptComp->GetAwoken())
				continue;
			scriptComp->Awake();
			scriptComp->SetAwoken(true);
		}

		for (ScriptComponent* scriptComp : m_ScriptComponents)
		{
			if (scriptComp->GetStarted())
				continue;
			scriptComp->Start();
			scriptComp->SetStarted(true);
		}

		for (ScriptComponent* scriptComp : m_ScriptComponents)
		{
			scriptComp->Update();
		}

		for (ScriptComponent* scriptComp : m_ScriptComponents)
		{
			scriptComp->LateUpdate();
		}
	}

	void GameObject::BuildRenderScene(RenderScene& renderScene, const AssetManager& assets, const Renderer& renderer) const
	{
		for (RenderableComponent* renderableComponent : m_RenderableComponents)
		{
			renderableComponent->BuildRenderScene(renderScene, assets, renderer);
		}
	}
}
