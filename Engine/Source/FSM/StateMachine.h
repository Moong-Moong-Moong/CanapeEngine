#pragma once

#include "FSM/FsmCondition.h"
#include "FSM/FsmData.h"
#include "FSM/FsmState.h"
#include "Scene/Component/Component.h"

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace Canape
{
	// The Scene starts and updates every StateMachine after script Update and before LateUpdate.
	class StateMachine : public Component
	{
		RTTR_ENABLE(Component)

	public:
		StateMachine() = default;

		// Parameters read by transition conditions. Unset parameters fail every comparison.
		void SetFloat(const std::string& name, float value) { m_Parameters[name] = value; }
		void SetInt(const std::string& name, int value) { m_Parameters[name] = static_cast<float>(value); }
		void SetBool(const std::string& name, bool value) { m_Parameters[name] = value ? 1.0f : 0.0f; }
		float GetFloat(const std::string& name) const;
		int GetInt(const std::string& name) const { return static_cast<int>(GetFloat(name)); }
		bool GetBool(const std::string& name) const { return GetFloat(name) != 0.0f; }

		//*** Driven by a ScriptComponent: call Initialize once (e.g. in Start), then Update every frame
		//*** after setting the parameters for that frame. The Scene does not run a StateMachine.

		// Builds the runtime states from the definition. No state is entered yet: the initial state is entered by the first Update.
		// Calling it again rebuilds the states from scratch without calling Exit on the old ones.
		void Initialize();
		bool IsInitialized() const { return m_Initialized; }

		// Enters the initial state on the first call, then checks transitions and updates the active states.
		// Does nothing before Initialize or while the component is disabled.
		void Update(float deltaTime);

		std::string GetCurrentStateName() const;

		// Innermost (leaf) active state.
		FsmState* GetCurrentState() const { return m_Current; }

		// True when the named state is the current state or one of its ancestors.
		bool IsStateActive(const std::string& name) const;

		// True when typeName is a compiled FsmState class.
		static bool IsStateClassRegistered(const std::string& typeName);

		//***** Definition (serialized, edited in the editor)
		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		const std::vector<FsmParameterDesc>& GetParameterDescs() const { return m_ParameterDescs; }
		void SetParameterDescs(const std::vector<FsmParameterDesc>& descs) { m_ParameterDescs = descs; }

		const std::vector<FsmConditionDesc>& GetConditionDescs() const { return m_ConditionDescs; }
		void SetConditionDescs(const std::vector<FsmConditionDesc>& descs) { m_ConditionDescs = descs; }

		const std::vector<FsmStateDesc>& GetStateDescs() const { return m_StateDescs; }
		void SetStateDescs(const std::vector<FsmStateDesc>& descs) { m_StateDescs = descs; }

		// Root state entered by Start.
		const std::string& GetInitialState() const { return m_InitialState; }
		void SetInitialState(const std::string& name) { m_InitialState = name; }

		//*** Parameters
		const FsmParameterDesc* FindParameterDesc(const std::string& name) const;
		std::vector<std::string> GetParameterNames() const;
		// Adds a parameter named baseName (numbered if taken) and returns its name.
		std::string AddParameter(const std::string& baseName = "NewParameter");
		void RemoveParameter(const std::string& name);
		// Fails on an empty or taken name. Comparisons follow the new name.
		bool RenameParameter(const std::string& name, const std::string& newName);
		// Resets the default value and fixes comparisons that no longer fit the type.
		void SetParameterType(const std::string& name, FsmParameterType type);
		void SetParameterDefault(const std::string& name, float value);

		//*** Conditions
		const FsmConditionDesc* FindConditionDesc(const std::string& name) const;
		std::vector<std::string> GetConditionNames() const;
		std::string AddCondition(const std::string& baseName = "NewCondition");
		void RemoveCondition(const std::string& name);
		// Fails on an empty or taken name. Transitions follow the new name.
		bool RenameCondition(const std::string& name, const std::string& newName);

		void AddComparison(const std::string& condition);
		void RemoveComparison(const std::string& condition, int index);
		// Also switches the comparison to one that fits the parameter type.
		void SetComparisonParameter(const std::string& condition, int index, const std::string& parameter);
		// Ignored when the comparison does not fit the parameter type.
		void SetComparisonCompare(const std::string& condition, int index, FsmCompare compare);
		void SetComparisonThreshold(const std::string& condition, int index, float threshold);
		// Comparisons that fit the parameter. All of them for an unknown parameter.
		std::vector<FsmCompare> GetCompareOptions(const std::string& parameter) const;

		//*** States
		const FsmStateDesc* FindStateDesc(const std::string& name) const;
		std::vector<std::string> GetStateNames() const;
		// States with no parent or a missing parent.
		std::vector<std::string> GetRootStateNames() const;
		std::vector<std::string> GetChildStateNames(const std::string& name) const;
		// True when the state is entered by default: the initial root, or its parent's initial child.
		bool IsInitialState(const std::string& name) const;

		// Checks a state name before AddState. Returns false with a message.
		bool CanAddState(const std::string& name, const std::string& parent, std::string& error) const;
		// The first child of a parent (or the first root) becomes its initial state.
		bool AddState(const std::string& name, const std::string& type, const std::string& parent, std::string& error);
		// Fails while the state has children.
		bool RemoveState(const std::string& name);
		// child must be a child of the state, or empty.
		void SetStateInitialChild(const std::string& name, const std::string& child);

		void AddTransition(const std::string& state);
		void RemoveTransition(const std::string& state, int index);
		void SetTransitionTarget(const std::string& state, int index, const std::string& target);
		void SetTransitionCondition(const std::string& state, int index, const std::string& condition);

	private:
		void ChangeState(FsmState* target);
		//*** target에서 공통 조상 상태 이전까지 Enter()호출
		void EnterFrom(FsmState* ancestor, FsmState* target);
		// Runs the state's one-time Initialize before its first Enter.
		void EnterState(FsmState* state);
		bool IsActive(const FsmState* state) const;
		//*** 조상까지 현재 활성화중인 상태를 불러온다.
		std::vector<FsmState*> GetActiveChain() const;
		bool IsConditionMet(const std::string& name) const;
		bool Evaluate(const FsmCondition& condition) const;

		FsmParameterDesc* FindParameter(const std::string& name);
		FsmConditionDesc* FindCondition(const std::string& name);
		FsmStateDesc* FindState(const std::string& name);
		FsmCondition* FindComparison(const std::string& condition, int index);
		FsmTransitionDesc* FindTransition(const std::string& state, int index);

		std::string m_Name;
		std::vector<FsmParameterDesc> m_ParameterDescs;
		std::vector<FsmConditionDesc> m_ConditionDescs;
		std::vector<FsmStateDesc> m_StateDescs;
		std::string m_InitialState;

		// int and bool are stored as float.
		std::unordered_map<std::string, float> m_Parameters;
		std::vector<std::unique_ptr<FsmState>> m_States;
		FsmState* m_Current = nullptr;
		// Set by Initialize, entered by the next Update.
		FsmState* m_PendingInitial = nullptr;
		bool m_Initialized = false;
		bool m_WarnedNotInitialized = false;
	};
}
