#include "Scene/Component/ScriptComponent.h"
#include "Scene/Scene.h"

namespace Canape
{
	Scene* ScriptComponent::GetOwnerScene() const
	{
		GameObject* obj = GetOwnerObject();
		return obj ? obj->GetScene() : nullptr;
	}

	Handle<GameObject> ScriptComponent::CreateGameObject(const std::string& name) const
	{
		Scene* scene = GetOwnerScene();
		return scene ? scene->AddGameObject(name) : Handle<GameObject>{};
	}

	Handle<GameObject> ScriptComponent::Instantiate(const std::string& prefabPath, TransformComponent* parent) const
	{
		Scene* scene = GetOwnerScene();
		return scene ? scene->Instantiate(prefabPath, parent) : Handle<GameObject>{};
	}

	void ScriptComponent::Destroy(const Handle<GameObject>& handle) const
	{
		if (Scene* scene = GetOwnerScene())
		{
			scene->RemoveGameObject(handle);
		}
	}

	void ScriptComponent::Destroy(GameObject* gameObject) const
	{
		if (Scene* scene = GetOwnerScene())
		{
			scene->RemoveGameObject(gameObject);
		}
	}

	GameObject* ScriptComponent::GetGameObject(const Handle<GameObject>& handle) const
	{
		Scene* scene = GetOwnerScene();
		return scene ? scene->GetGameObject(handle) : nullptr;
	}

	Handle<GameObject> ScriptComponent::FindGameObjectHandleByName(const std::string& name) const
	{
		Scene* scene = GetOwnerScene();
		return scene ? scene->FindGameObjectHandleByName(name) : Handle<GameObject>{};
	}

	GameObject* ScriptComponent::FindGameObjectByName(const std::string& name) const
	{
		Scene* scene = GetOwnerScene();
		return scene ? scene->FindGameObjectByName(name) : nullptr;
	}
}
