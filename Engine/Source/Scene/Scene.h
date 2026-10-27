#pragma once

#include "Scene/GameObject/GameObjectManager.h"
#include "Scene/Component/ComponentRegistry.h"


#include <string>
#include <vector>
#include <queue>

namespace Canape
{
	class GameObject;
	class Component;
	class CameraComponent;
	class AssetManager;
	class Renderer;
	struct RenderScene;

	class Scene final
	{
	public:
		explicit Scene(std::string name);
		~Scene();

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		const std::string& GetName() const { return m_Name; }

		void BeginUpdate();
		void Update();
		void EndUpdate();
		void OnDestroyScripts();

		void BuildRenderScene(RenderScene& renderScene, const AssetManager& assets, const Renderer& renderer);

		//***** GameObject
		Handle<GameObject> AddGameObject(const std::string& name = "GameObject");
		void RemoveGameObject(const Handle<GameObject> handle);
		GameObject* GetGameObject(const Handle<GameObject> handle);
		Handle<GameObject> FindGameObjectByName(const std::string& name);

		//***** Component
		void RegisterComponent(Component* component);
		void UnregisterComponent(Component* component);

		//***** Camera
		CameraComponent* GetMainCamera() ;
		void SetMainCamera(CameraComponent* camera);

	private:
		void UpdateScripts();

		//*** 컴포넌트가 실행될 수 있는가?
		static bool IsRunnable(const Component* component);

	private:
		friend class SceneManager;
		friend class GameObject;

		std::string m_Name;

		std::queue<Component*> m_onDestroyQueue;
		ComponentRegistry m_componentRegistry;
		GameObjectManager m_gameObjects;
		Handle<GameObject> m_MainCamera;
	};
}
