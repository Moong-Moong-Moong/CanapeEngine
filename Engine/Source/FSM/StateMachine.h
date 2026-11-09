#pragma once

#include "FSM/FsmCondition.h"
#include "FSM/FsmState.h"
#include "Scene/Component/Component.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <string>
#include <vector>

namespace Canape
{
	class StateMachine : public Component
	{
		RTTR_ENABLE(Component)

	public:
		StateMachine() = default;

		template<typename T, typename... Args>
		T* CreateCondition(Args&&... args);

		template<typename T, typename... Args>
		T* CreateState(FsmState* parent, Args&&... args);

		void Start(FsmState* initial);
		void Update(float deltaTime);

		std::string GetCurrentStateName() const;

		// Innermost (leaf) active state.
		FsmState* GetCurrentState() const { return m_Current; }

	private:
		void ChangeState(FsmState* target);
		//*** target에서 공통 조상 상태 이전까지 Enter()호출
		void EnterFrom(FsmState* ancestor, FsmState* target);
		bool IsActive(const FsmState* state) const;
		//*** 조상까지 현재 활성화중인 상태를 불러온다.
		std::vector<FsmState*> GetActiveChain() const;
		std::vector<std::unique_ptr<FsmCondition>> m_Conditions;
		std::vector<std::unique_ptr<FsmState>> m_States;
		FsmState* m_Current = nullptr;
	};

	template<typename T, typename... Args>
	T* StateMachine::CreateCondition(Args&&... args)
	{
		static_assert(std::is_base_of_v<FsmCondition, T>, "T must derive from FsmCondition");

		auto condition = std::make_unique<T>(std::forward<Args>(args)...);
		T* result = condition.get();
		m_Conditions.push_back(std::move(condition));
		return result;
	}

	template<typename T, typename... Args>
	T* StateMachine::CreateState(FsmState* parent, Args&&... args)
	{
		static_assert(std::is_base_of_v<FsmState, T>, "T must derive from FsmState");

		auto state = std::make_unique<T>(std::forward<Args>(args)...);
		T* result = state.get();
		result->m_Parent = parent;
		m_States.push_back(std::move(state));
		return result;
	}
}
