#include "FSM/StateMachine.h"
#include "CNDebug/DebugLog.h"

#include <rttr/type>

#include <algorithm>

namespace Canape
{
	namespace
	{
		FsmState* CreateStateObject(const std::string& typeName)
		{
			const rttr::type type = rttr::type::get_by_name(typeName);
			if (!type.is_valid() || !type.is_derived_from<FsmState>())
			{
				return nullptr;
			}

			rttr::variant created = type.create();
			bool ok = false;
			FsmState* state = created.is_valid() ? created.convert<FsmState*>(&ok) : nullptr;
			if (!ok || !state)
			{
				if (created.is_valid())
				{
					type.destroy(created);
				}
				return nullptr;
			}
			return state;
		}
	}

	void StateMachine::Start()
	{
		m_Started = true;
		m_States.clear();
		m_Current = nullptr;

		// Values a script already set this frame win over the defaults.
		for (const FsmParameterDesc& desc : m_ParameterDescs)
		{
			m_Parameters.try_emplace(desc.name, desc.defaultValue);
		}

		std::unordered_map<std::string, FsmState*> states;
		for (const FsmStateDesc& desc : m_StateDescs)
		{
			FsmState* state = CreateStateObject(desc.type);
			if (!state)
			{
				CN_LOG_RUNTIME("FSM", "State class not found (rebuild needed?): ", desc.type);
				continue;
			}
			state->m_Name = desc.name;
			states[desc.name] = state;
			m_States.emplace_back(state);
		}

		const auto find = [&states](const std::string& name) -> FsmState*
		{
			const auto it = states.find(name);
			return it != states.end() ? it->second : nullptr;
		};

		for (const FsmStateDesc& desc : m_StateDescs)
		{
			FsmState* state = find(desc.name);
			if (!state)
			{
				continue;
			}
			state->m_Parent = find(desc.parent);
		}

		for (const FsmStateDesc& desc : m_StateDescs)
		{
			FsmState* state = find(desc.name);
			if (!state)
			{
				continue;
			}

			FsmState* child = find(desc.initialChild);
			state->m_InitialChild = child && child->m_Parent == state ? child : nullptr;

			for (const FsmTransitionDesc& transition : desc.transitions)
			{
				if (FsmState* target = find(transition.target))
				{
					state->m_Transitions.push_back({ transition.condition, target });
				}
			}
		}

		if (FsmState* initial = find(m_InitialState))
		{
			EnterFrom(nullptr, initial);
		}
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
				if (IsConditionMet(transition.condition))
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

	float StateMachine::GetFloat(const std::string& name) const
	{
		const auto it = m_Parameters.find(name);
		return it != m_Parameters.end() ? it->second : 0.0f;
	}

	bool StateMachine::IsConditionMet(const std::string& name) const
	{
		const auto it = std::find_if(m_ConditionDescs.begin(), m_ConditionDescs.end(),
			[&name](const FsmConditionDesc& desc) { return desc.name == name; });
		if (it == m_ConditionDescs.end())
		{
			return false;
		}

		return std::all_of(it->comparisons.begin(), it->comparisons.end(),
			[this](const FsmCondition& comparison) { return Evaluate(comparison); });
	}

	bool StateMachine::Evaluate(const FsmCondition& condition) const
	{
		const auto it = m_Parameters.find(condition.parameter);
		if (it == m_Parameters.end())
		{
			return false;
		}

		const float value = it->second;
		switch (condition.compare)
		{
		case FsmCompare::Greater:  return value > condition.threshold;
		case FsmCompare::Less:     return value < condition.threshold;
		case FsmCompare::Equal:    return value == condition.threshold;
		case FsmCompare::NotEqual: return value != condition.threshold;
		case FsmCompare::True:     return value != 0.0f;
		case FsmCompare::False:    return value == 0.0f;
		}
		return false;
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
