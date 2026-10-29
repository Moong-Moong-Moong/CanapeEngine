#pragma once

#include "Scene/Component/Component.h"
#include "Scene/Component/TransformComponent.h"
#include "Core/Containers/SlotMap.h"
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Canape
{
	class Scene;

	class GameObject final
	{
	public:
		GameObject();
		~GameObject() = default;
		GameObject(const GameObject&) = delete;
		GameObject& operator=(const GameObject&) = delete;

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }
		Scene* GetScene() const { return m_Scene; }
		void SetScene(Scene* scene) { m_Scene = scene; }
		void SetHandle(const Handle<GameObject>& handle) { m_handle = handle; }
		const Handle<GameObject>& GetHandle() const { return m_handle; }
		bool IsActive() const { return m_Active; }
		void SetActive(bool active) { m_Active = active; }
		bool IsPendingDestroy() const { return m_PendingDestroy; }
		void SetPendingDestroy(bool pending) { m_PendingDestroy = pending; }

		template<typename T, typename... Args>
		T* AddComponent(Args&&... args);
		template<typename T>
		T* GetComponent() const;
		template<typename T>
		std::vector<T*> GetComponents() const;
		template<typename T>
		bool RemoveComponent();

		bool RemoveComponent(Component* component);
		void DestroyComponent(Component* component);

		void RegisterComponent(Component* component);
		void RemoveAllComponents();

		TransformComponent* GetTransform() const { return m_Transform; }

	private:
		std::string m_Name;
		Scene* m_Scene = nullptr;
		Handle<GameObject> m_handle = {};

		bool m_Active = true;
		bool m_PendingDestroy = false;

		std::vector<std::unique_ptr<Component>> m_Components;
		TransformComponent* m_Transform = nullptr;
	};

	template<typename T, typename... Args>
	T* GameObject::AddComponent(Args&&... args)
	{
		static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
		static_assert(!std::is_same_v<T, TransformComponent>, "TransformComponent is added automatically");

		auto component = std::make_unique<T>(std::forward<Args>(args)...);
		T* result = component.get();
		component->m_GameObject = this;
		component->m_Enabled = true;
		m_Components.push_back(std::move(component));
		RegisterComponent(result);
		return result;
	}

	template<typename T>
	T* GameObject::GetComponent() const
	{
		static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

		for (const auto& component : m_Components)
		{
			if (component->m_PendingDestroy)
			{
				continue;
			}

			if (T* result = dynamic_cast<T*>(component.get()))
			{
				return result;
			}
		}
		return nullptr;
	}

	template<typename T>
	std::vector<T*> GameObject::GetComponents() const
	{
		static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

		std::vector<T*> result;
		for (const auto& component : m_Components)
		{
			if (component->m_PendingDestroy)
			{
				continue;
			}

			if (T* casted = dynamic_cast<T*>(component.get()))
			{
				result.push_back(casted);
			}
		}
		return result;
	}

	template<typename T>
	bool GameObject::RemoveComponent()
	{
		static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
		static_assert(!std::is_same_v<T, TransformComponent>, "TransformComponent cannot be removed");

		for (const auto& component : m_Components)
		{
			if (component->m_PendingDestroy)
			{
				continue;
			}

			if (dynamic_cast<T*>(component.get()))
			{
				return RemoveComponent(component.get());
			}
		}
		return false;
	}
}
