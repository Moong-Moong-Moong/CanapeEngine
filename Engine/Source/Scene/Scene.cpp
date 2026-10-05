#include "Scene/Scene.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/CameraComponent.h"

namespace Canape
{
	Scene::Scene(std::string name)
		: m_Name(std::move(name))
	{
	}

	Scene::~Scene()
	{
		m_Objects.Clear();
	}

	GameObject* Scene::CreateGameObject(const std::string& name)
	{
		return m_Objects.Get(m_Objects.Create(this, name));
	}

	void Scene::Destroy(GameObject* gameObject)
	{
		if (!gameObject || gameObject->GetScene() != this || gameObject->m_PendingDestroy)
		{
			return;
		}

		MarkForDestroy(gameObject);
	}

	void Scene::Destroy(const GameObjectHandle& handle)
	{
		Destroy(m_Objects.Get(handle));
	}

	GameObject* Scene::Find(std::string_view name) const
	{
		GameObject* found = nullptr;
		m_Objects.ForEach([&](GameObject* gameObject)
		{
			if (!found && !gameObject->m_PendingDestroy && gameObject->GetName() == name)
			{
				found = gameObject;
			}
		});
		return found;
	}

	std::vector<GameObject*> Scene::GetRootGameObjects() const
	{
		std::vector<GameObject*> roots;
		m_Objects.ForEach([&](GameObject* gameObject)
		{
			if (!gameObject->m_PendingDestroy && !gameObject->GetTransform()->GetParent())
			{
				roots.push_back(gameObject);
			}
		});
		return roots;
	}

	CameraComponent* Scene::GetMainCamera() const
	{
		GameObject* gameObject = m_Objects.Get(m_MainCamera);
		return gameObject ? gameObject->GetComponent<CameraComponent>() : nullptr;
	}

	void Scene::SetMainCamera(CameraComponent* camera)
	{
		if (!camera)
		{
			m_MainCamera = {};
			return;
		}

		GameObject* gameObject = camera->GetGameObject();
		if (!gameObject || gameObject->GetScene() != this)
		{
			return;
		}

		m_MainCamera = gameObject->GetHandle();
	}

	void Scene::Update()
	{
		m_Objects.ForEach([](GameObject* gameObject)
		{
			if (!gameObject->m_PendingDestroy && gameObject->IsActiveInHierarchy())
			{
				gameObject->Update();
			}
		});
	}

	void Scene::LateUpdate()
	{
		m_Objects.ForEach([](GameObject* gameObject)
		{
			if (!gameObject->m_PendingDestroy && gameObject->IsActiveInHierarchy())
			{
				gameObject->LateUpdate();
			}
		});
	}

	void Scene::FlushDestroyed()
	{
		std::vector<GameObjectHandle> destroyed;
		m_Objects.ForEach([&](GameObject* gameObject)
		{
			gameObject->FlushDestroyedComponents();

			if (gameObject->m_PendingDestroy)
			{
				destroyed.push_back(gameObject->GetHandle());
			}
		});

		for (const GameObjectHandle& handle : destroyed)
		{
			m_Objects.Destroy(handle);
		}
	}

	void Scene::MarkForDestroy(GameObject* gameObject)
	{
		gameObject->m_PendingDestroy = true;

		for (TransformComponent* child : gameObject->GetTransform()->GetChildren())
		{
			MarkForDestroy(child->GetGameObject());
		}
	}
}
