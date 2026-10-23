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

	}

	void Scene::UpdateGameObjects()
	{
		m_gameObjects.Update();
	}

	void Scene::BuildRenderScene(RenderScene& renderScene, const AssetManager& assets, const Renderer& renderer)
	{
		m_gameObjects.BuildRenderScene(renderScene, assets, renderer);
	}

	Handle<GameObject> Scene::AddGameObject(const std::string& name)
	{
		return m_gameObjects.AddGameObject(name, this);
	}

	void Scene::RemoveGameObject(const Handle<GameObject> handle)
	{
		m_gameObjects.RemoveGameObject(handle);
	}

	Handle<GameObject> Scene::DeferAddGameObject(const std::string& name)
	{
		return m_gameObjects.DeferAddGameObject(name, this);
	}

	void Scene::DeferRemoveGameObject(const Handle<GameObject> handle)
	{
		//*** 다음 프레임 시작 전 삭제
		m_gameObjects.DeferRemoveGameObject(handle);
	}	

	GameObject* Scene::GetGameObject(const Handle<GameObject> handle)
	{
		return m_gameObjects.GetGameObject(handle);
	}

	Handle<GameObject> Scene::FindGameObjectByName(const std::string& name)
	{
		return m_gameObjects.FindGameObjectByName(name);
	}

	CameraComponent* Scene::GetMainCamera()
	{
		GameObject* gameObject = m_gameObjects.GetGameObject(m_MainCamera);
		if (!gameObject)
		{
			return nullptr;
		}

		return gameObject->GetComponent<CameraComponent>();
	}

	void Scene::SetMainCamera(CameraComponent* camera)
	{
		if (!camera || !camera->GetOwnerObject() ||
			camera->GetOwnerObject()->GetScene() != this)
		{
			m_MainCamera = {};
			return;
		}

		m_MainCamera = camera->GetOwnerObject()->GetHandle();
	}
}
