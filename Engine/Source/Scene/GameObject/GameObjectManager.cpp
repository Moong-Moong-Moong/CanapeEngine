#include "Scene/GameObject/GameObjectManager.h"
#include "Scene/GameObject/GameObject.h"

namespace Canape
{
	Handle<GameObject> GameObjectManager::AddGameObject(const std::string& name, Scene* scene)
	{
		Handle<GameObject> handle = m_gameObjects.Add();
		GameObject* newGameObject = m_gameObjects.Get(handle);
		newGameObject->SetName(name);
		newGameObject->SetScene(scene);
		newGameObject->SetHandle(handle);
		return handle;
	}

	void GameObjectManager::RemoveGameObject(Handle<GameObject> handle)
	{
		GameObject* gameObject = m_gameObjects.Get(handle);
		if (!gameObject || gameObject->IsPendingDestroy())
		{
			return;
		}

		gameObject->SetPendingDestroy(true);
		gameObject->RemoveAllComponents();
		m_pendingRemove.push_back(handle);
	}

	GameObject* GameObjectManager::GetGameObject(Handle<GameObject> handle)
	{
		return m_gameObjects.Get(handle);
	}

	Handle<GameObject> GameObjectManager::FindGameObjectByName(const std::string& name)
	{
		Handle<GameObject> handle =
			m_gameObjects.Find([&](GameObject* obj)->bool
			{
				return obj->GetName() == name;
			});

		return handle;
	}

	void GameObjectManager::FlushPending()
	{
		//*** 삭제 예정 오브젝트 삭제
		for (Handle<GameObject> handle : m_pendingRemove)
		{
			m_gameObjects.Remove(handle);
		}
		m_pendingRemove.clear();
	}
}
