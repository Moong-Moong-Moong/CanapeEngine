#pragma once

#include <vector>
#include <string>

namespace Canape
{
	class FsmCondition;

	class FsmState
	{
	public:
		virtual ~FsmState() = default;

		virtual void Enter() {}
		virtual void Exit() {}
		virtual void Update(float dT) {}

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		void AddTransition(FsmCondition* condition, FsmState* target) { m_Transitions.push_back({ condition, target }); }
		void SetInitialChild(FsmState* child) { m_InitialChild = child; }

		FsmState* GetParent() const { return m_Parent; }

	private:
		friend class StateMachine;

		struct Transition
		{
			FsmCondition* condition;
			FsmState* target;
		};

		std::string m_Name;
		std::vector<Transition> m_Transitions;
		FsmState* m_Parent = nullptr;
		FsmState* m_InitialChild = nullptr;
	};
}
