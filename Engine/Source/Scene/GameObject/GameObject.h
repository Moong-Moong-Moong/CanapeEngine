#pragma once

#include "Scene/Component/Component.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/GameObject/GameObjectHandle.h"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Canape
{
	class Scene;
	class MonoBehaviour;

	class GameObject final
	{
	public:
		~GameObject();

		GameObject(const GameObject&) = delete;
		GameObject& operator=(const GameObject&) = delete;

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		const GameObjectHandle& GetHandle() const { return m_Handle; }
		Scene* GetScene() const { return m_Scene; }
		TransformComponent* GetTransform() const { return m_Transform; }

		bool IsActiveSelf() const { return m_Active; }
		bool IsActiveInHierarchy() const;
		void SetActive(bool active);

		bool IsPendingDestroy() const { return m_PendingDestroy; }

		template<typename T, typename... Args>
		T* AddComponent(Args&&... args);

		template<typename T>
		T* GetComponent() const;

		template<typename T>
		std::vector<T*> GetComponents() const;

		template<typename T>
		bool RemoveComponent();

		bool RemoveComponent(Component* component);

	private:
		friend class Scene;
		friend class GameObjectManager;
		friend class Component;
		friend class TransformComponent;

		GameObject(Scene* scene, std::string name, GameObjectHandle handle);

		void AttachComponent(std::unique_ptr<Component> component);
		void Update();
		void LateUpdate();
		void FlushDestroyedComponents();

		void OnComponentEnabledChanged(Component* component);
		void RefreshHierarchy();
		static void SyncBehaviour(MonoBehaviour* behaviour);
		static void DestroyBehaviour(MonoBehaviour* behaviour);

		std::string m_Name;
		GameObjectHandle m_Handle;
		Scene* m_Scene = nullptr;
		TransformComponent* m_Transform = nullptr;
		std::vector<std::unique_ptr<Component>> m_Components;
		std::vector<MonoBehaviour*> m_Behaviours;
		bool m_Active = true;
		bool m_PendingDestroy = false;
	};

	template<typename T, typename... Args>
	T* GameObject::AddComponent(Args&&... args)
	{
		static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
		static_assert(!std::is_same_v<T, TransformComponent>, "TransformComponent is added automatically");

		auto component = std::make_unique<T>(std::forward<Args>(args)...);
		T* result = component.get();
		AttachComponent(std::move(component));
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
