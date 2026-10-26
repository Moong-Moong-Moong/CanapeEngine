#include "Scene/Scene.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/RenderableComponent.h"
#include "Scene/Component/ScriptComponent.h"

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
		//***** Component 지연 처리
		std::vector<Component*> pendingRemoveComps = m_componentRegistry.GetPendingRemove();
		//*** ComponentRegistry 정리
		m_componentRegistry.FlushPending();
		//*** 실제 컴포넌트 제거
		for (Component* comp : pendingRemoveComps)
		{
			comp->GetOwnerObject()->DestroyComponent(comp);
		}

		//***** GameObject 지연 처리
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
		OnDestroyScripts();
	}

	void Scene::OnDestroyScripts()
	{
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

		for (ScriptComponent* script : scripts)
		{
			if (!IsRunnable(script) || script->GetAwoken())
				continue;
			script->Awake();
			script->SetAwoken(true);
		}

		for (ScriptComponent* script : scripts)
		{
			if (!IsRunnable(script) || script->GetStarted())
				continue;
			script->Start();
			script->SetStarted(true);
		}

		for (ScriptComponent* script : scripts)
		{
			if (IsRunnable(script))
				script->Update();
		}

		for (ScriptComponent* script : scripts)
		{
			if (IsRunnable(script))
				script->LateUpdate();
		}
	}

	void Scene::BuildRenderScene(RenderScene& renderScene, const AssetManager& assets, const Renderer& renderer)
	{
		for (RenderableComponent* renderable : m_componentRegistry.GetRenderables())
		{
			if (IsRunnable(renderable))
				renderable->BuildRenderScene(renderScene, assets, renderer);
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
		return m_gameObjects.AddGameObject(name, this);
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
