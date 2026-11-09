#include "FSM/StateMachine.h"
#include "CNDebug/DebugLog.h"

#include <rttr/type>

#include <algorithm>
#include <utility>

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

		template <typename T>
		T* FindByName(std::vector<T>& items, const std::string& name)
		{
			const auto it = std::find_if(items.begin(), items.end(), [&name](const T& item) { return item.name == name; });
			return it != items.end() ? &*it : nullptr;
		}

		template <typename T>
		const T* FindByName(const std::vector<T>& items, const std::string& name)
		{
			const auto it = std::find_if(items.begin(), items.end(), [&name](const T& item) { return item.name == name; });
			return it != items.end() ? &*it : nullptr;
		}

		template <typename T>
		std::vector<std::string> Names(const std::vector<T>& items)
		{
			std::vector<std::string> names;
			for (const T& item : items)
			{
				names.push_back(item.name);
			}
			return names;
		}

		template <typename T>
		std::string MakeUniqueName(const std::vector<T>& items, const std::string& base)
		{
			std::string name = base;
			for (int index = 1; FindByName(items, name); ++index)
			{
				name = base + std::to_string(index);
			}
			return name;
		}

		template <typename T>
		bool IsValidIndex(const std::vector<T>& items, int index)
		{
			return index >= 0 && index < static_cast<int>(items.size());
		}

		bool IsBoolCompare(FsmCompare compare)
		{
			return compare == FsmCompare::True || compare == FsmCompare::False;
		}

		// Bool parameters use True/False, the others use the numeric comparisons.
		bool FitsParameter(FsmCompare compare, const FsmParameterDesc* parameter)
		{
			return !parameter || (parameter->type == FsmParameterType::Bool) == IsBoolCompare(compare);
		}

		void FitToParameter(FsmCondition& comparison, const FsmParameterDesc* parameter)
		{
			if (!FitsParameter(comparison.compare, parameter))
			{
				comparison.compare = parameter->type == FsmParameterType::Bool ? FsmCompare::True : FsmCompare::Greater;
			}
		}
	}


	void StateMachine::Initialize()
	{
		m_Current = nullptr;
		m_PendingInitial = nullptr;
		m_States.clear();
		m_Initialized = true;
		m_WarnedNotInitialized = false;

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
			state->m_Machine = this;
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

		m_PendingInitial = find(m_InitialState);
	}

	void StateMachine::Update(float deltaTime)
	{
		if (!m_Initialized)
		{
			if (!m_WarnedNotInitialized)
			{
				CN_LOG_RUNTIME("FSM", "Update called before Initialize: ", m_Name);
				m_WarnedNotInitialized = true;
			}
			return;
		}
		if (!IsEnabled())
		{
			return;
		}

		// Entered here rather than in Initialize, so states first touch other components during Update.
		if (FsmState* initial = std::exchange(m_PendingInitial, nullptr))
		{
			EnterFrom(nullptr, initial);
		}
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
			EnterState(*it);
		}

		m_Current = target;
		while (m_Current->m_InitialChild)
		{
			m_Current = m_Current->m_InitialChild;
			EnterState(m_Current);
		}
	}

	void StateMachine::EnterState(FsmState* state)
	{
		if (!state->m_Initialized)
		{
			state->m_Initialized = true;
			state->Initialize();
		}
		state->Enter();
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

	bool StateMachine::IsStateActive(const std::string& name) const
	{
		for (const FsmState* state = m_Current; state; state = state->m_Parent)
		{
			if (state->GetName() == name)
			{
				return true;
			}
		}
		return false;
	}

	bool StateMachine::IsStateClassRegistered(const std::string& typeName)
	{
		const rttr::type type = rttr::type::get_by_name(typeName);
		return type.is_valid() && type.is_derived_from<FsmState>();
	}

	//***** Parameters

	FsmParameterDesc* StateMachine::FindParameter(const std::string& name)
	{
		return FindByName(m_ParameterDescs, name);
	}

	const FsmParameterDesc* StateMachine::FindParameterDesc(const std::string& name) const
	{
		return FindByName(m_ParameterDescs, name);
	}

	std::vector<std::string> StateMachine::GetParameterNames() const
	{
		return Names(m_ParameterDescs);
	}

	std::string StateMachine::AddParameter(const std::string& baseName)
	{
		FsmParameterDesc parameter;
		parameter.name = MakeUniqueName(m_ParameterDescs, baseName);
		m_ParameterDescs.push_back(parameter);
		return parameter.name;
	}

	void StateMachine::RemoveParameter(const std::string& name)
	{
		std::erase_if(m_ParameterDescs, [&name](const FsmParameterDesc& desc) { return desc.name == name; });
	}

	bool StateMachine::RenameParameter(const std::string& name, const std::string& newName)
	{
		FsmParameterDesc* parameter = FindParameter(name);
		if (!parameter || newName.empty() || newName == name || FindParameter(newName))
		{
			return false;
		}

		for (FsmConditionDesc& condition : m_ConditionDescs)
		{
			for (FsmCondition& comparison : condition.comparisons)
			{
				if (comparison.parameter == name)
				{
					comparison.parameter = newName;
				}
			}
		}
		parameter->name = newName;
		return true;
	}

	void StateMachine::SetParameterType(const std::string& name, FsmParameterType type)
	{
		FsmParameterDesc* parameter = FindParameter(name);
		if (!parameter || parameter->type == type)
		{
			return;
		}

		parameter->type = type;
		parameter->defaultValue = 0.0f;
		for (FsmConditionDesc& condition : m_ConditionDescs)
		{
			for (FsmCondition& comparison : condition.comparisons)
			{
				if (comparison.parameter == name)
				{
					FitToParameter(comparison, parameter);
				}
			}
		}
	}

	void StateMachine::SetParameterDefault(const std::string& name, float value)
	{
		if (FsmParameterDesc* parameter = FindParameter(name))
		{
			switch (parameter->type)
			{
			case FsmParameterType::Bool: parameter->defaultValue = value != 0.0f ? 1.0f : 0.0f; break;
			case FsmParameterType::Int:  parameter->defaultValue = static_cast<float>(static_cast<int>(value)); break;
			default:                     parameter->defaultValue = value; break;
			}
		}
	}

	//***** Conditions

	FsmConditionDesc* StateMachine::FindCondition(const std::string& name)
	{
		return FindByName(m_ConditionDescs, name);
	}

	const FsmConditionDesc* StateMachine::FindConditionDesc(const std::string& name) const
	{
		return FindByName(m_ConditionDescs, name);
	}

	std::vector<std::string> StateMachine::GetConditionNames() const
	{
		return Names(m_ConditionDescs);
	}

	std::string StateMachine::AddCondition(const std::string& baseName)
	{
		FsmConditionDesc condition;
		condition.name = MakeUniqueName(m_ConditionDescs, baseName);
		m_ConditionDescs.push_back(condition);
		return condition.name;
	}

	void StateMachine::RemoveCondition(const std::string& name)
	{
		std::erase_if(m_ConditionDescs, [&name](const FsmConditionDesc& desc) { return desc.name == name; });
	}

	bool StateMachine::RenameCondition(const std::string& name, const std::string& newName)
	{
		FsmConditionDesc* condition = FindCondition(name);
		if (!condition || newName.empty() || newName == name || FindCondition(newName))
		{
			return false;
		}

		for (FsmStateDesc& state : m_StateDescs)
		{
			for (FsmTransitionDesc& transition : state.transitions)
			{
				if (transition.condition == name)
				{
					transition.condition = newName;
				}
			}
		}
		condition->name = newName;
		return true;
	}

	FsmCondition* StateMachine::FindComparison(const std::string& condition, int index)
	{
		FsmConditionDesc* desc = FindCondition(condition);
		return desc && IsValidIndex(desc->comparisons, index) ? &desc->comparisons[index] : nullptr;
	}

	void StateMachine::AddComparison(const std::string& condition)
	{
		if (FsmConditionDesc* desc = FindCondition(condition))
		{
			FsmCondition comparison;
			comparison.compare = FsmCompare::Greater;
			desc->comparisons.push_back(comparison);
		}
	}

	void StateMachine::RemoveComparison(const std::string& condition, int index)
	{
		FsmConditionDesc* desc = FindCondition(condition);
		if (desc && IsValidIndex(desc->comparisons, index))
		{
			desc->comparisons.erase(desc->comparisons.begin() + index);
		}
	}

	void StateMachine::SetComparisonParameter(const std::string& condition, int index, const std::string& parameter)
	{
		if (FsmCondition* comparison = FindComparison(condition, index))
		{
			comparison->parameter = parameter;
			FitToParameter(*comparison, FindParameterDesc(parameter));
		}
	}

	void StateMachine::SetComparisonCompare(const std::string& condition, int index, FsmCompare compare)
	{
		FsmCondition* comparison = FindComparison(condition, index);
		if (comparison && FitsParameter(compare, FindParameterDesc(comparison->parameter)))
		{
			comparison->compare = compare;
		}
	}

	void StateMachine::SetComparisonThreshold(const std::string& condition, int index, float threshold)
	{
		if (FsmCondition* comparison = FindComparison(condition, index))
		{
			const FsmParameterDesc* parameter = FindParameterDesc(comparison->parameter);
			comparison->threshold = parameter && parameter->type == FsmParameterType::Int
				? static_cast<float>(static_cast<int>(threshold))
				: threshold;
		}
	}

	std::vector<FsmCompare> StateMachine::GetCompareOptions(const std::string& parameter) const
	{
		const FsmParameterDesc* desc = FindParameterDesc(parameter);
		std::vector<FsmCompare> options;
		for (FsmCompare compare : { FsmCompare::Greater, FsmCompare::Less, FsmCompare::Equal, FsmCompare::NotEqual, FsmCompare::True, FsmCompare::False })
		{
			if (FitsParameter(compare, desc))
			{
				options.push_back(compare);
			}
		}
		return options;
	}

	//***** States

	FsmStateDesc* StateMachine::FindState(const std::string& name)
	{
		return FindByName(m_StateDescs, name);
	}

	const FsmStateDesc* StateMachine::FindStateDesc(const std::string& name) const
	{
		return FindByName(m_StateDescs, name);
	}

	std::vector<std::string> StateMachine::GetStateNames() const
	{
		return Names(m_StateDescs);
	}

	std::vector<std::string> StateMachine::GetRootStateNames() const
	{
		std::vector<std::string> roots;
		for (const FsmStateDesc& state : m_StateDescs)
		{
			if (state.parent.empty() || !FindStateDesc(state.parent))
			{
				roots.push_back(state.name);
			}
		}
		return roots;
	}

	std::vector<std::string> StateMachine::GetChildStateNames(const std::string& name) const
	{
		std::vector<std::string> children;
		for (const FsmStateDesc& state : m_StateDescs)
		{
			if (state.parent == name)
			{
				children.push_back(state.name);
			}
		}
		return children;
	}

	bool StateMachine::IsInitialState(const std::string& name) const
	{
		const FsmStateDesc* state = FindStateDesc(name);
		if (!state)
		{
			return false;
		}
		const FsmStateDesc* parent = FindStateDesc(state->parent);
		return parent ? parent->initialChild == name : m_InitialState == name;
	}

	bool StateMachine::CanAddState(const std::string& name, const std::string& parent, std::string& error) const
	{
		if (name.empty() || name.find('/') != std::string::npos)
		{
			error = "Enter a state name without '/'.";
			return false;
		}
		if (FindStateDesc(name))
		{
			error = "A state named '" + name + "' already exists.";
			return false;
		}
		if (!parent.empty() && !FindStateDesc(parent))
		{
			error = "Parent state '" + parent + "' does not exist.";
			return false;
		}
		return true;
	}

	bool StateMachine::AddState(const std::string& name, const std::string& type, const std::string& parent, std::string& error)
	{
		if (!CanAddState(name, parent, error))
		{
			return false;
		}

		FsmStateDesc state;
		state.name = name;
		state.type = type;
		state.parent = parent;
		m_StateDescs.push_back(state);

		if (FsmStateDesc* parentDesc = FindState(parent))
		{
			if (parentDesc->initialChild.empty())
			{
				parentDesc->initialChild = name;
			}
		}
		else if (m_InitialState.empty())
		{
			m_InitialState = name;
		}
		return true;
	}

	bool StateMachine::RemoveState(const std::string& name)
	{
		if (!FindStateDesc(name) || !GetChildStateNames(name).empty())
		{
			return false;
		}

		if (m_InitialState == name)
		{
			m_InitialState.clear();
		}
		for (FsmStateDesc& other : m_StateDescs)
		{
			if (other.initialChild == name)
			{
				other.initialChild.clear();
			}
		}
		std::erase_if(m_StateDescs, [&name](const FsmStateDesc& desc) { return desc.name == name; });
		return true;
	}

	void StateMachine::SetStateInitialChild(const std::string& name, const std::string& child)
	{
		FsmStateDesc* state = FindState(name);
		if (!state)
		{
			return;
		}
		const FsmStateDesc* childDesc = FindStateDesc(child);
		if (child.empty() || (childDesc && childDesc->parent == name))
		{
			state->initialChild = child;
		}
	}

	FsmTransitionDesc* StateMachine::FindTransition(const std::string& state, int index)
	{
		FsmStateDesc* desc = FindState(state);
		return desc && IsValidIndex(desc->transitions, index) ? &desc->transitions[index] : nullptr;
	}

	void StateMachine::AddTransition(const std::string& state)
	{
		if (FsmStateDesc* desc = FindState(state))
		{
			desc->transitions.push_back({});
		}
	}

	void StateMachine::RemoveTransition(const std::string& state, int index)
	{
		FsmStateDesc* desc = FindState(state);
		if (desc && IsValidIndex(desc->transitions, index))
		{
			desc->transitions.erase(desc->transitions.begin() + index);
		}
	}

	void StateMachine::SetTransitionTarget(const std::string& state, int index, const std::string& target)
	{
		if (target == state)
		{
			return;
		}
		if (FsmTransitionDesc* transition = FindTransition(state, index))
		{
			transition->target = target;
		}
	}

	void StateMachine::SetTransitionCondition(const std::string& state, int index, const std::string& condition)
	{
		if (FsmTransitionDesc* transition = FindTransition(state, index))
		{
			transition->condition = condition;
		}
	}
}
