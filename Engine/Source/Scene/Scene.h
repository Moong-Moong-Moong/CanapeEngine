#pragma once

#include "Scene/GameObject/GameObjectHandle.h"
#include "Scene/GameObject/GameObjectManager.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Canape
{
	class GameObject;
	class CameraComponent;

	class Scene final
	{
	public:
		explicit Scene(std::string name);
		~Scene();

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		const std::string& GetName() const { return m_Name; }

		GameObject* CreateGameObject(const std::string& name = "GameObject");
		void Destroy(GameObject* gameObject);
		void Destroy(const GameObjectHandle& handle);

		GameObject* GetGameObject(const GameObjectHandle& handle) const { return m_Objects.Get(handle); }
		bool IsAlive(const GameObjectHandle& handle) const { return m_Objects.IsAlive(handle); }

		GameObject* Find(std::string_view name) const;
		std::vector<GameObject*> GetRootGameObjects() const;
		std::size_t GetGameObjectCount() const { return m_Objects.GetCount(); }

		template<typename Func>
		void ForEachGameObject(Func&& func) const { m_Objects.ForEach(std::forward<Func>(func)); }

		CameraComponent* GetMainCamera() const;
		void SetMainCamera(CameraComponent* camera);

	private:
		friend class SceneManager;

		void Update();
		void LateUpdate();
		void FlushDestroyed();
		void MarkForDestroy(GameObject* gameObject);

		std::string m_Name;
		GameObjectManager m_Objects;
		GameObjectHandle m_MainCamera;
	};
}
