#include "Scene/Component/MonoBehaviour.h"
#include "Scene/Scene.h"

namespace Canape
{
	GameObject* MonoBehaviour::CreateGameObject(const std::string& name) const
	{
		return GetGameObject()->GetScene()->CreateGameObject(name);
	}

	GameObject* MonoBehaviour::Resolve(const GameObjectHandle& handle) const
	{
		return GetGameObject()->GetScene()->GetGameObject(handle);
	}

	void MonoBehaviour::Destroy(GameObject* gameObject) const
	{
		if (gameObject)
		{
			gameObject->GetScene()->Destroy(gameObject);
		}
	}

	void MonoBehaviour::Destroy(const GameObjectHandle& handle) const
	{
		GetGameObject()->GetScene()->Destroy(handle);
	}

	void MonoBehaviour::Destroy(Component* component) const
	{
		if (component && component->GetGameObject())
		{
			component->GetGameObject()->RemoveComponent(component);
		}
	}
}
