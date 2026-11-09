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

		// Builds the runtime states from the definition and enters the initial state.
		void Start();
		void Update(float deltaTime);
		bool IsStarted() const { return m_Started; }

		std::string GetCurrentStateName() const;

		// Innermost (leaf) active state.
		FsmState* GetCurrentState() const { return m_Current; }

		//***** Definition (serialized, edited in the editor)
		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		const std::vector<FsmParameterDesc>& GetParameterDescs() const { return m_ParameterDescs; }
		void SetParameterDescs(const std::vector<FsmParameterDesc>& descs) { m_ParameterDescs = descs; }
		std::vector<FsmParameterDesc>& EditParameterDescs() { return m_ParameterDescs; }

		const std::vector<FsmConditionDesc>& GetConditionDescs() const { return m_ConditionDescs; }
		void SetConditionDescs(const std::vector<FsmConditionDesc>& descs) { m_ConditionDescs = descs; }
		std::vector<FsmConditionDesc>& EditConditionDescs() { return m_ConditionDescs; }

		const std::vector<FsmStateDesc>& GetStateDescs() const { return m_StateDescs; }
		void SetStateDescs(const std::vector<FsmStateDesc>& descs) { m_StateDescs = descs; }
		std::vector<FsmStateDesc>& EditStateDescs() { return m_StateDescs; }

		// Root state entered by Start.
		const std::string& GetInitialState() const { return m_InitialState; }
		void SetInitialState(const std::string& name) { m_InitialState = name; }

	private:
		void ChangeState(FsmState* target);
		//*** target에서 공통 조상 상태 이전까지 Enter()호출
		void EnterFrom(FsmState* ancestor, FsmState* target);
		bool IsActive(const FsmState* state) const;
		//*** 조상까지 현재 활성화중인 상태를 불러온다.
		std::vector<FsmState*> GetActiveChain() const;
		bool IsConditionMet(const std::string& name) const;
		bool Evaluate(const FsmCondition& condition) const;

		std::string m_Name;
		std::vector<FsmParameterDesc> m_ParameterDescs;
		std::vector<FsmConditionDesc> m_ConditionDescs;
		std::vector<FsmStateDesc> m_StateDescs;
		std::string m_InitialState;

		// int and bool are stored as float.
		std::unordered_map<std::string, float> m_Parameters;
		std::vector<std::unique_ptr<FsmState>> m_States;
		FsmState* m_Current = nullptr;
		bool m_Started = false;
	};
}
