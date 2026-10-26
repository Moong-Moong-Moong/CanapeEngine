#pragma once

#include "Core/Containers/SlotMap.h"

#include <string>
#include <vector>

namespace Canape
{
	class GameObject;
	class Scene;

	class GameObjectManager
	{
	public:
		GameObjectManager() = default;
		~GameObjectManager() = default;

		GameObjectManager(const GameObjectManager&) = delete;
		GameObjectManager& operator=(const GameObjectManager&) = delete;

		Handle<GameObject> AddGameObject(const std::string& name, Scene* scene);
		void RemoveGameObject(Handle<GameObject> handle);
		GameObject* GetGameObject(Handle<GameObject> handle);
		Handle<GameObject> FindGameObjectByName(const std::string& name);

		void FlushPending();

	private:
		SlotMap<GameObject> m_gameObjects;
		std::vector<Handle<GameObject>> m_pendingRemove;
	};
}
