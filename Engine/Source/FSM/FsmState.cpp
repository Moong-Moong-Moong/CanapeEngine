#include "FSM/FsmState.h"
#include "FSM/StateMachine.h"
#include "Scene/GameObject/GameObject.h"

namespace Canape
{
	GameObject* FsmState::GetOwnerObject() const
	{
		return m_Machine ? m_Machine->GetOwnerObject() : nullptr;
	}

	TransformComponent* FsmState::GetTransform() const
	{
		return m_Machine ? m_Machine->GetTransform() : nullptr;
	}

	Scene* FsmState::GetScene() const
	{
		GameObject* owner = GetOwnerObject();
		return owner ? owner->GetScene() : nullptr;
	}
}
