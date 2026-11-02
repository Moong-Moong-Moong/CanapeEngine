#include "Scene/Scene.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/FogComponent.h"
#include "Scene/Component/LightComponent.h"
#include "Scene/Component/RenderableComponent.h"
#include "Scene/Component/ScriptComponent.h"
#include "Scene/Component/SkyboxComponent.h"
#include "Renderer/Renderer.h"

namespace Canape
{
	Scene::Scene(std::string name)
		: m_Name(std::move(name))
	{
	}

	Scene::~Scene()
	{
		OnDestroyScripts();
	}

	void Scene::BeginUpdate()
	{
		//***** Component
		std::vector<Component*> pendingRemoveComps = m_componentRegistry.GetPendingRemove();
		//*** ComponentRegistry 정리
		m_componentRegistry.FlushPending();
		//*** 실제 컴포넌트 제거
		for (Component* comp : pendingRemoveComps)
		{
			comp->GetOwnerObject()->DestroyComponent(comp);
		}

		//***** GameObject
		m_gameObjects.FlushPending();
	}

	void Scene::Update()
	{
		BeginUpdate();
		UpdateScripts();
		EndUpdate();
	}

	void Scene::EndUpdate()
	{
		//***파괴된 스크립트의 OnDestroy 실행
		OnDestroyScripts();
	}

	void Scene::OnDestroyScripts()
	{
		//*** 연쇄 OnDestroy를 처리
		while (!m_onDestroyQueue.empty())
		{
			Component* component = m_onDestroyQueue.front();
			m_onDestroyQueue.pop();

			ScriptComponent* script = dynamic_cast<ScriptComponent*>(component);
			if (script && script->GetAwoken())
			{
				script->OnDestroy();
			}
		}
	}

	void Scene::UpdateScripts()
	{
		std::span<ScriptComponent* const> scripts = m_componentRegistry.GetScripts();

		//***Awake
		for (ScriptComponent* script : scripts)
		{
			if (!IsRunnable(script) || script->GetAwoken())
				continue;
			script->Awake();
			script->SetAwoken(true);
		}

		//***Start
		for (ScriptComponent* script : scripts)
		{
			if (!IsRunnable(script) || script->GetStarted())
				continue;
			script->Start();
			script->SetStarted(true);
		}

		//***Update
		for (ScriptComponent* script : scripts)
		{
			if (IsRunnable(script))
				script->Update();
		}

		//***LateUpdate
		for (ScriptComponent* script : scripts)
		{
			if (IsRunnable(script))
				script->LateUpdate();
		}
	}

	void Scene::BuildRenderScene(const AssetManager& assets, Renderer& renderer)
	{
		//***Mesh
		for (RenderableComponent* renderable : m_componentRegistry.GetRenderables())
		{
			if (IsRunnable(renderable))
				renderable->AddRenderItem(assets, renderer);
		}

		//***Light
		for (LightComponent* light : m_componentRegistry.GetLights())
		{
			if (IsRunnable(light))
				renderer.AddLight(light->BuildRenderLight());
		}

		//***Fog
		for (FogComponent* fog : m_componentRegistry.GetFogs())
		{
			if (IsRunnable(fog))
			{
				renderer.SetFog(fog->BuildRenderFog());
				break;
			}
		}

		//***SkyBox
		for (SkyboxComponent* skybox : m_componentRegistry.GetSkyboxes())
		{
			if (IsRunnable(skybox))
			{
				renderer.SetSkybox(skybox->GetCubemap().GetId(), skybox->BuildRenderSkybox());
				renderer.SetAmbient(skybox->GetAmbientColor(), skybox->GetAmbientIntensity());
				break;
			}
		}
	}

	void Scene::RegisterComponent(Component* component)
	{
		m_componentRegistry.Register(component);
	}

	void Scene::UnregisterComponent(Component* component)
	{
		if (!component)
			return;
		m_componentRegistry.Unregister(component);
		m_onDestroyQueue.push(component);
	}

	bool Scene::IsRunnable(const Component* component)
	{
		const GameObject* owner = component->GetOwnerObject();
		return component->IsEnabled() && !component->IsPendingDestroy() && owner && owner->IsActive();
	}

	Handle<GameObject> Scene::AddGameObject(const std::string& name)
	{
		Handle<GameObject> handle = m_gameObjects.AddGameObject(name, this);
		if (GameObject* gameObject = m_gameObjects.GetGameObject(handle))
		{
			gameObject->SetId(GenerateId());
			gameObject->GetTransform()->SetId(GenerateId());
		}
		return handle;
	}

	void Scene::RemoveGameObject(const Handle<GameObject> handle)
	{
		m_gameObjects.RemoveGameObject(handle);
	}	

	GameObject* Scene::GetGameObject(const Handle<GameObject> handle)
	{
		return m_gameObjects.GetGameObject(handle);
	}

	Handle<GameObject> Scene::FindGameObjectByName(const std::string& name)
	{
		return m_gameObjects.FindGameObjectByName(name);
	}

	Handle<GameObject> Scene::FindGameObjectById(uint64_t id)
	{
		for (GameObject* gameObject : m_gameObjects.GetAllGameObjects())
		{
			if (gameObject->GetId() == id && !gameObject->IsPendingDestroy())
			{
				return gameObject->GetHandle();
			}
		}
		return {};
	}

	std::vector<GameObject*> Scene::GetAllGameObjects()
	{
		return m_gameObjects.GetAllGameObjects();
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
