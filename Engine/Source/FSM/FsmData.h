#pragma once

#include "FSM/FsmCondition.h"

#include <string>
#include <vector>

namespace Canape
{
	// Serialized definition of a StateMachine. Everything refers to other entries by name.

	enum class FsmParameterType
	{
		Float,
		Int,
		Bool,
	};

	struct FsmParameterDesc
	{
		std::string name;
		FsmParameterType type = FsmParameterType::Float;
		float defaultValue = 0.0f;
	};

	// A named condition passes when every comparison passes.
	struct FsmConditionDesc
	{
		std::string name;
		std::vector<FsmCondition> comparisons;
	};

	struct FsmTransitionDesc
	{
		std::string target;
		std::string condition;
	};

	struct FsmStateDesc
	{
		std::string name;
		std::string type;			// Registered FsmState class name.
		std::string parent;			// Empty for a root state.
		std::string initialChild;
		std::vector<FsmTransitionDesc> transitions;
	};
}
