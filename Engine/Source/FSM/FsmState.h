#pragma once

#include <rttr/rttr_enable.h>

#include <string>
#include <vector>

namespace Canape
{
	// Base class of every state. Derived classes are created by their RTTR name,
	// so they need RTTR_ENABLE(Canape::FsmState) and a registered default constructor.
	class FsmState
	{
		RTTR_ENABLE()

	public:
		virtual ~FsmState() = default;

		virtual void Enter() {}
		virtual void Exit() {}
		virtual void Update(float /*dT*/) {}

		const std::string& GetName() const { return m_Name; }
		FsmState* GetParent() const { return m_Parent; }

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
	};
}
