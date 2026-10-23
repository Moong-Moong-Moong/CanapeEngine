#pragma once

#include "Core/Containers/SlotMap.h"

#include <string>
#include <vector>

namespace Canape
{
	class GameObject;
	class Scene;
	class AssetManager;
	class Renderer;
	struct RenderScene;

	class GameObjectManager
	{
	public:
		GameObjectManager() = default;
		~GameObjectManager() = default;

		GameObjectManager(const GameObjectManager&) = delete;
		GameObjectManager& operator=(const GameObjectManager&) = delete;

		Handle<GameObject> AddGameObject(const std::string& name, Scene* scene);
		void RemoveGameObject(Handle<GameObject> handle);
		Handle<GameObject> DeferAddGameObject(const std::string& name, Scene* scene);
		void DeferRemoveGameObject(Handle<GameObject> handle);
		GameObject* GetGameObject(Handle<GameObject> handle);
		Handle<GameObject> FindGameObjectByName(const std::string& name);

		void BeginUpdate();
		void Update();
		void BuildRenderScene(RenderScene& renderScene, const AssetManager& assets, const Renderer& renderer);

		void FlushRemoved();
		void ActivePending();
	private:


	private:
		SlotMap<GameObject> m_gameObjects;
		std::vector<Handle<GameObject>> m_pendingRemoveHandles;
	};
}
