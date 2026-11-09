#pragma once

#include <string>

namespace Canape
{
	enum class FsmCompare
	{
		Greater,
		Less,
		Equal,
		NotEqual,
		True,
		False,
	};

	// Compares one StateMachine parameter. threshold is unused for True/False.
	struct FsmCondition
	{
		std::string parameter;
		FsmCompare compare = FsmCompare::True;
		float threshold = 0.0f;
	};
}
