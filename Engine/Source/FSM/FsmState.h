#pragma once

#include <rttr/rttr_enable.h>

#include <string>
#include <vector>

namespace Canape
{
	class GameObject;
	class Scene;
	class StateMachine;
	class TransformComponent;

	// Base class of every state. Derived classes are created by their RTTR name,
	// so they need RTTR_ENABLE(Canape::FsmState) and a registered default constructor.
	class FsmState
	{
		RTTR_ENABLE()

	public:
		virtual ~FsmState() = default;

		// Called once, right before the first Enter. Every component has finished Awake/Start by then,
		// so this is the place to look up and cache other components.
		virtual void Initialize() {}
		virtual void Enter() {}
		virtual void Exit() {}
		virtual void Update(float /*dT*/) {}

		const std::string& GetName() const { return m_Name; }
		FsmState* GetParent() const { return m_Parent; }

		//*** Context: the StateMachine running this state and the GameObject that owns it.
		//*** For components and the rest of the scene, use the GameObject / Scene API directly,
		//*** e.g. GetOwnerObject()->GetComponent<T>() or GetScene()->FindGameObjectByName(name).
		StateMachine* GetStateMachine() const { return m_Machine; }
		GameObject* GetOwnerObject() const;
		TransformComponent* GetTransform() const;
		Scene* GetScene() const;

	private:
		friend class StateMachine;

		struct Transition
		{
			std::string condition;
			FsmState* target;
		};

		std::string m_Name;
		std::vector<Transition> m_Transitions;
		FsmState* m_Parent = nullptr;
		FsmState* m_InitialChild = nullptr;
		StateMachine* m_Machine = nullptr;
		bool m_Initialized = false;
	};
}
