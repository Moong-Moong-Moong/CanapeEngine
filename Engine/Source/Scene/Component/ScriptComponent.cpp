#include "Scene/Component/ScriptComponent.h"
#include "Scene/Scene.h"

namespace Canape
{
	Handle<GameObject> ScriptComponent::CreateGameObject(const std::string& name) const
	{
		Handle<GameObject> handle = {};
		GameObject* obj = GetOwnerObject();
		if (obj == nullptr)
			return handle;
		Scene* scene = obj->GetScene();
		if (scene == nullptr)
			return handle;

		return scene->AddGameObject(name);
	}

	void ScriptComponent::Destroy(const Handle<GameObject>& handle) const
	{
		GameObject* obj = GetOwnerObject();
		if (obj == nullptr)
			return;
		Scene* scene = obj->GetScene();
		if (scene == nullptr)
			return;

		scene->RemoveGameObject(handle);
	}

	GameObject* ScriptComponent::GetGameObject(const Handle<GameObject>& handle) const
	{
		GameObject* obj = GetOwnerObject();
		if (obj == nullptr)
			return nullptr;
		Scene* scene = obj->GetScene();
		if (scene == nullptr)
			return nullptr;

		return scene->GetGameObject(handle);
	}
}
