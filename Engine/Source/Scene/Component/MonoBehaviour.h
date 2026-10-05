#pragma once

#include "Scene/Component/Component.h"
#include "Scene/GameObject/GameObject.h"

#include <string>
#include <utility>
#include <vector>

namespace Canape
{
	class MonoBehaviour : public Component
	{
	public:
		template<typename T>
		T* GetComponent() const { return GetGameObject()->GetComponent<T>(); }

		template<typename T>
		std::vector<T*> GetComponents() const { return GetGameObject()->GetComponents<T>(); }

		template<typename T, typename... Args>
		T* AddComponent(Args&&... args) const { return GetGameObject()->AddComponent<T>(std::forward<Args>(args)...); }

		GameObject* CreateGameObject(const std::string& name = "GameObject") const;
		GameObject* Resolve(const GameObjectHandle& handle) const;
		void Destroy(GameObject* gameObject) const;
		void Destroy(const GameObjectHandle& handle) const;
		void Destroy(Component* component) const;

	protected:
		MonoBehaviour() = default;

		virtual void Awake() {}
		virtual void OnEnable() {}
		virtual void Start() {}
		virtual void Update() {}
		virtual void LateUpdate() {}
		virtual void OnDisable() {}
		virtual void OnDestroy() {}

	private:
		friend class GameObject;

		bool m_Awoken = false;
		bool m_Started = false;
		bool m_EnabledState = false;
	};
}
