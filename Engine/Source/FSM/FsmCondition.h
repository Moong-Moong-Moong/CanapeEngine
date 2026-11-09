#pragma once

namespace Canape
{
	class FsmCondition
	{
	public:
		virtual ~FsmCondition() = default;

		virtual bool Evaluate() const = 0;
	};
}
