#pragma once

#include "Physics/PhysicsScene.h"
#include "Scene/GameObject/GameObjectManager.h"
#include "Scene/Component/ComponentRegistry.h"


#include <cstdint>
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

		//***** Rendering
		//*** Scene에서 렌더링에 필요한 요소들을 Renderer에 전달.
		void BuildRenderScene(const AssetManager& assets, Renderer& renderer);

		//***** GameObject
		Handle<GameObject> AddGameObject(const std::string& name = "GameObject");
		void RemoveGameObject(const Handle<GameObject> handle);
		GameObject* GetGameObject(const Handle<GameObject> handle);
		Handle<GameObject> FindGameObjectByName(const std::string& name);
		Handle<GameObject> FindGameObjectById(uint64_t id);
		std::vector<GameObject*> GetAllGameObjects();

		uint64_t GenerateId() { return ++m_LastId; }
		void ReserveId(uint64_t id) { m_LastId = id > m_LastId ? id : m_LastId; }

		//***** Component
		//*** 컴포넌트 등록시 ComponentRegistry에 타입별로 등록되어 컴포넌트 순회 시 사용됨.
		void RegisterComponent(Component* component);
		void UnregisterComponent(Component* component);
		//*** 컴포넌트가 실행될 수 있는가?
		static bool IsRunnable(const Component* component);

		//***** Camera
		CameraComponent* GetMainCamera() ;
		void SetMainCamera(CameraComponent* camera);

		//***** Physics
		PhysicsScene& GetPhysics() { return m_Physics; }
		const PhysicsScene& GetPhysics() const { return m_Physics; }

	private:
		void RunAwakeAndStart();
		void RunFixedStep();
		void RunUpdate();

	private:
		friend class SceneManager;
		friend class GameObject;

		std::string m_Name;

		PhysicsScene m_Physics;

		std::queue<Component*> m_onDestroyQueue;
		ComponentRegistry m_componentRegistry;
		GameObjectManager m_gameObjects;
		Handle<GameObject> m_MainCamera;
		uint64_t m_LastId = 0;
	};
}
