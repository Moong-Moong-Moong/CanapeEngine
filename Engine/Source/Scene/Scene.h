#pragma once

#include "Physics/PhysicsScene.h"
#include "Scene/GameObject/GameObjectManager.h"
#include "Scene/Component/ComponentRegistry.h"


#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <queue>

namespace Canape
{
	class GameObject;
	class Component;
	class TransformComponent;
	class CameraComponent;
	class AssetManager;
	class Renderer;
	class JoltRuntime;
	class Session;
	class SimulationScene;
	class Simulator;

	class Scene final
	{
	public:
		explicit Scene(std::string name, JoltRuntime* physicsRuntime = nullptr, Session* simulationSession = nullptr);
		~Scene();

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		const std::string& GetName() const { return m_Name; }
		void SetName(std::string name) { m_Name = std::move(name); }

		void BeginUpdate();
		void Update();
		void EndUpdate();
		void OnDestroyScripts();

		//***** Rendering
		//*** Scene에서 렌더링에 필요한 요소들을 Renderer에 전달.
		void BuildRenderScene(const AssetManager& assets, Renderer& renderer);

		//***** GameObject
		Handle<GameObject> AddGameObject(const std::string& name = "GameObject");
		Handle<GameObject> Instantiate(const std::string& prefabPath, TransformComponent* parent = nullptr);
		Handle<GameObject> FindGameObjectHandleByName(const std::string& name);
		Handle<GameObject> FindGameObjectById(uint64_t id);

		GameObject* GetGameObject(const Handle<GameObject> handle);
		GameObject* FindGameObjectByName(const std::string& name);

		void RemoveGameObject(const Handle<GameObject> handle);
		void RemoveGameObject(GameObject* gameObject);

		std::vector<GameObject*> GetAllGameObjects();

		uint64_t GenerateId() { return ++m_LastId; }
		void ReserveId(uint64_t id) { m_LastId = id > m_LastId ? id : m_LastId; }

		//***** Component
		//*** 컴포넌트 등록시 ComponentRegistry에 타입별로 등록되어 컴포넌트 순회 시 사용됨.
		void RegisterComponent(Component* component);
		void UnregisterComponent(Component* component);
		//*** 컴포넌트가 실행될 수 있는가?
		static bool IsRunnable(const Component* component);

		//*** 등록된 UI 캔버스(실행 가능 여부는 IsRunnable로 확인)
		std::span<UICanvasComponent* const> GetUICanvases() const { return m_componentRegistry.GetUICanvases(); }
		//*** 등록된 조명/카메라(에디터 아이콘 등). 실행 가능 여부는 IsRunnable로 확인
		std::span<LightComponent* const> GetLights() const { return m_componentRegistry.GetLights(); }
		std::span<CameraComponent* const> GetCameras() const { return m_componentRegistry.GetCameras(); }

		//***** Camera
		CameraComponent* GetMainCamera() ;
		void SetMainCamera(CameraComponent* camera);

		//***** Physics
		PhysicsScene& GetPhysics() { return m_Physics; }
		const PhysicsScene& GetPhysics() const { return m_Physics; }

		//***** Simulation
		// Null when the scene was made without a session.
		SimulationScene* GetSimulation() { return m_Simulation.get(); }
		// Once a frame, after the update and before the scene is rendered.
		void Simulate(Simulator&);

	private:
		void RunAwakeAndStart();
		void RunFixedStep();
		void RunUpdate();

	private:
		friend class SceneManager;
		friend class GameObject;

		std::string m_Name;

		PhysicsScene m_Physics;
		std::unique_ptr<SimulationScene> m_Simulation;

		std::queue<Component*> m_onDestroyQueue;
		ComponentRegistry m_componentRegistry;
		GameObjectManager m_gameObjects;
		Handle<GameObject> m_MainCamera;
		uint64_t m_LastId = 0;
	};
}
