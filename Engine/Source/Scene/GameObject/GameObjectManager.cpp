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
		m_gameObjects.Remove(handle);
	}

	Handle<GameObject> GameObjectManager::DeferAddGameObject(const std::string& name, Scene* scene)
	{
		Handle<GameObject> handle = AddGameObject(name, scene);
		GameObject* gameObject = m_gameObjects.Get(handle);
		gameObject->SetActive(false);
		gameObject->SetActivePending(true);
		return handle;
	}

	void GameObjectManager::DeferRemoveGameObject(Handle<GameObject> handle)
	{
		GameObject* gameObject = m_gameObjects.Get(handle);
		if (!gameObject || gameObject->IsPendingDestroy())
		{
			return;
		}

		gameObject->SetPendingDestroy(true);
		m_pendingRemoveHandles.push_back(handle);
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

		return Handle<GameObject>();
	}

	void GameObjectManager::BeginUpdate()
	{
		//*** 삭제 예정 오브젝트 삭제
		FlushRemoved();
		//*** 활성화 예정 오브젝트 활성화
		ActivePending();

		const std::vector<GameObject*> gameObjects = m_gameObjects.GetAll();
		for (GameObject* gameObject : gameObjects)
		{
			gameObject->BeginUpdate();
		}
	}

	void GameObjectManager::Update()
	{
		BeginUpdate();
		
		for (GameObject* obj : m_gameObjects.GetAll())
		{
			obj->Update();
		}
	}

	void GameObjectManager::BuildRenderScene(RenderScene& renderScene, const AssetManager& assets, const Renderer& renderer)
	{
		for (GameObject* gameObject : m_gameObjects.GetAll())
		{
			if (gameObject->IsActive())
				continue;

			gameObject->BuildRenderScene(renderScene, assets, renderer);
		}
	}

	void GameObjectManager::FlushRemoved()
	{
		for (Handle<GameObject> handle : m_pendingRemoveHandles)
		{
			m_gameObjects.Remove(handle);
		}

		m_pendingRemoveHandles.clear();
	}

	void GameObjectManager::ActivePending()
	{
		std::vector<Handle<GameObject>> objs =
			m_gameObjects.Finds([&](GameObject* obj)->bool
				{
					return obj->IsActivePending();
				});

		for (Handle<GameObject> obj : objs)
		{
			GameObject* gameObject = m_gameObjects.Get(obj);
			gameObject->SetActivePending(false);
			gameObject->SetActive(true);
		}
	}
}
