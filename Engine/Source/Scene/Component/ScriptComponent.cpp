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

	Handle<GameObject> ScriptComponent::CreateGameObject(const std::string& prefabPath, TransformComponent* parent) const
	{
		Scene* scene = GetOwnerScene();
		return scene ? scene->AddGameObject(prefabPath, parent) : Handle<GameObject>{};
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
		if (gameObject)
		{
			Destroy(gameObject->GetHandle());
		}
	}

	GameObject* ScriptComponent::GetGameObject(const Handle<GameObject>& handle) const
	{
		Scene* scene = GetOwnerScene();
		return scene ? scene->GetGameObject(handle) : nullptr;
	}

	Handle<GameObject> ScriptComponent::FindGameObjectHandle(const std::string& name) const
	{
		Scene* scene = GetOwnerScene();
		return scene ? scene->FindGameObjectByName(name) : Handle<GameObject>{};
	}

	GameObject* ScriptComponent::FindGameObject(const std::string& name) const
	{
		return GetGameObject(FindGameObjectHandle(name));
	}
}
