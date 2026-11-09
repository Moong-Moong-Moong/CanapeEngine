// rttr declares the element wrapper of a sequence container it never defines.
#pragma warning(disable : 4506)

#include "Core/Reflection/Reflection.h"
#include "FSM/StateMachine.h"

namespace Canape
{
	void RegisterStateMachineTypes()
	{
		using namespace rttr;

		registration::enumeration<FsmCompare>("FsmCompare")
		(
			value("Greater", FsmCompare::Greater),
			value("Less", FsmCompare::Less),
			value("Equal", FsmCompare::Equal),
			value("NotEqual", FsmCompare::NotEqual),
			value("True", FsmCompare::True),
			value("False", FsmCompare::False)
		);

		registration::enumeration<FsmParameterType>("FsmParameterType")
		(
			value("Float", FsmParameterType::Float),
			value("Int", FsmParameterType::Int),
			value("Bool", FsmParameterType::Bool)
		);

		registration::class_<FsmCondition>("FsmCondition")
			.constructor<>()(policy::ctor::as_object)
			.property("parameter", &FsmCondition::parameter)
			.property("compare", &FsmCondition::compare)
			.property("threshold", &FsmCondition::threshold);

		registration::class_<FsmParameterDesc>("FsmParameterDesc")
			.constructor<>()(policy::ctor::as_object)
			.property("name", &FsmParameterDesc::name)
			.property("type", &FsmParameterDesc::type)
			.property("defaultValue", &FsmParameterDesc::defaultValue);

		registration::class_<FsmConditionDesc>("FsmConditionDesc")
			.constructor<>()(policy::ctor::as_object)
			.property("name", &FsmConditionDesc::name)
			.property("comparisons", &FsmConditionDesc::comparisons);

		registration::class_<FsmTransitionDesc>("FsmTransitionDesc")
			.constructor<>()(policy::ctor::as_object)
			.property("target", &FsmTransitionDesc::target)
			.property("condition", &FsmTransitionDesc::condition);

		registration::class_<FsmStateDesc>("FsmStateDesc")
			.constructor<>()(policy::ctor::as_object)
			.property("name", &FsmStateDesc::name)
			.property("type", &FsmStateDesc::type)
			.property("parent", &FsmStateDesc::parent)
			.property("initialChild", &FsmStateDesc::initialChild)
			.property("transitions", &FsmStateDesc::transitions);

		// Plain state with no behaviour, e.g. a parent that only groups children.
		registration::class_<FsmState>("FsmState")
			.constructor<>()(policy::ctor::as_raw_ptr);

		registration::class_<StateMachine>("StateMachine")
			.constructor<>()(policy::ctor::as_raw_ptr)
			.property("name", &StateMachine::GetName, &StateMachine::SetName)
			.property("parameters", &StateMachine::GetParameterDescs, &StateMachine::SetParameterDescs)
			.property("conditions", &StateMachine::GetConditionDescs, &StateMachine::SetConditionDescs)
			.property("states", &StateMachine::GetStateDescs, &StateMachine::SetStateDescs)
			.property("initialState", &StateMachine::GetInitialState, &StateMachine::SetInitialState);
	}
}
