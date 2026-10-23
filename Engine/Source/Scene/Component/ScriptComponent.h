#pragma once

#include "Scene/Component/Component.h"
#include "Scene/GameObject/GameObject.h"

#include <string>
#include <utility>
#include <vector>

namespace Canape
{
	class ScriptComponent : public Component
	{
	public:
		template<typename T>
		T* GetComponent() const { return GetGameObject()->GetComponent<T>(); }

		template<typename T>
		std::vector<T*> GetComponents() const { return GetGameObject()->GetComponents<T>(); }

		template<typename T, typename... Args>
		T* AddComponent(Args&&... args) const { return GetGameObject()->AddComponent<T>(std::forward<Args>(args)...); }


		bool GetAwoken() const { return m_Awoken; }
		void SetAwoken(bool awoken) { m_Awoken = awoken; }
		bool GetStarted() const { return m_Started; }
		void SetStarted(bool started) { m_Started = started; }

		Handle<GameObject> CreateGameObject(const std::string& name = "GameObject") const;
		GameObject* GetGameObject(const Handle<GameObject>& handle) const;
		void Destroy(const Handle<GameObject>& handle) const;

	protected:
		ScriptComponent() = default;

		virtual void Awake() {}
		virtual void Start() {}
		virtual void Update() {}
		virtual void LateUpdate() {}
		virtual void OnDestroy() {}

	private:
		friend class GameObject;

		bool m_Awoken = false;
		bool m_Started = false;
	};
}
