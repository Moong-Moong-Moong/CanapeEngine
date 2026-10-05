#include "Core/Time.h"

namespace Canape
{
	Time::Clock::time_point Time::s_BaseTime{};
	Time::Clock::time_point Time::s_PrevTime{};
	float Time::s_DeltaTime = 0.0f;

	void Time::Reset()
	{
		s_BaseTime = Clock::now();
		s_PrevTime = s_BaseTime;
		s_DeltaTime = 0.0f;
	}

	void Time::Tick()
	{
		const auto currTime = Clock::now();

		s_DeltaTime =
			static_cast<float>(
				std::chrono::duration<double>(
					currTime - s_PrevTime
				).count()
				);

		s_PrevTime = currTime;
	}
}
