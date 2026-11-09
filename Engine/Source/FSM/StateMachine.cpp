#include "FSM/StateMachine.h"

#include <algorithm>

namespace Canape
{
	void StateMachine::Start(FsmState* initial)
	{
		m_Current = nullptr;
		//*** nullptr = 트리로 치면 루트전까지 Enter() 실행
		EnterFrom(nullptr, initial);
	}

	void StateMachine::Update(float deltaTime)
	{
		if (!m_Current)
		{
			return;
		}

		bool changed = false;
		for (FsmState* state : GetActiveChain())
		{
			for (const FsmState::Transition& transition : state->m_Transitions)
			{
				if (transition.condition->Evaluate())
				{
					ChangeState(transition.target);
					changed = true;
					break;
				}
			}

			if (changed)
			{
				break;
			}
		}

		for (FsmState* state : GetActiveChain())
		{
			state->Update(deltaTime);
		}
	}

	void StateMachine::ChangeState(FsmState* target)
	{

		if (IsActive(target))
		{
			return;
		}

		// Deepest active state that is also an ancestor of target.
		FsmState* common = target->m_Parent;
		while (common && !IsActive(common))
		{
			common = common->m_Parent;
		}

		for (FsmState* state = m_Current; state != common; state = state->m_Parent)
		{
			state->Exit();
		}

		EnterFrom(common, target);
	}

	//*** target에서 공통 조상 상태 이전까지 Enter()호출
	void StateMachine::EnterFrom(FsmState* ancestor, FsmState* target)
	{
		std::vector<FsmState*> path;
		for (FsmState* state = target; state != ancestor; state = state->m_Parent)
		{
			path.push_back(state);
		}

		for (auto it = path.rbegin(); it != path.rend(); ++it)
		{
			(*it)->Enter();
		}

		m_Current = target;
		while (m_Current->m_InitialChild)
		{
			m_Current = m_Current->m_InitialChild;
			m_Current->Enter();
		}
	}

	bool StateMachine::IsActive(const FsmState* state) const
	{
		for (const FsmState* active = m_Current; active; active = active->m_Parent)
		{
			if (active == state)
			{
				return true;
			}
		}
		return false;
	}

	//*** 조상까지 현재 활성화중인 상태를 불러온다.
	std::vector<FsmState*> StateMachine::GetActiveChain() const
	{
		std::vector<FsmState*> chain;
		for (FsmState* state = m_Current; state; state = state->m_Parent)
		{
			chain.push_back(state);
		}
		std::reverse(chain.begin(), chain.end());
		return chain;
	}

	std::string StateMachine::GetCurrentStateName() const
	{
		std::string result;
		for (const FsmState* state : GetActiveChain())   // 루트 → 리프 순서
		{
			if (!result.empty())
			{
				result += '/';
			}
			result += state->GetName();
		}
		return result;
	}
}
