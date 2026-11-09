#include "Core/Time.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace Canape
{
	Time::Clock::time_point Time::s_BaseTime{};
	Time::Clock::time_point Time::s_PrevTime{};
	float Time::s_DeltaTime = 0.0f;
	float Time::s_UnscaledDeltaTime = 0.0f;
	float Time::s_TimeScale = 1.0f;
	float Time::s_FixedAccumulator = 0.0f;
	std::uint32_t Time::s_FixedStepCount = 0;

	void Time::SetTimeScale(float timeScale)
	{
		assert(std::isfinite(timeScale) && timeScale >= 0.0f);
		s_TimeScale = timeScale;
	}

	void Time::Reset()
	{
		s_PrevTime = s_BaseTime = Clock::now();

		s_DeltaTime = 0.0f;
		s_UnscaledDeltaTime = 0.0f;

		s_FixedAccumulator = 0.0f;
		s_FixedStepCount = 0;
	}

	void Time::Tick()
	{
		const auto currTime = Clock::now();
		s_UnscaledDeltaTime = std::chrono::duration<float>(currTime - s_PrevTime).count();
		s_PrevTime = currTime;

		s_DeltaTime = s_UnscaledDeltaTime * s_TimeScale;

		// Time beyond MaxFixedStepsPerFrame steps is dropped so a slow frame cannot snowball.
		constexpr float MaxBankedTime = MaxFixedStepsPerFrame * FixedStep;
		s_FixedAccumulator = std::min(s_FixedAccumulator + s_DeltaTime, MaxBankedTime);
		s_FixedStepCount = static_cast<std::uint32_t>(s_FixedAccumulator / FixedStep);
		s_FixedAccumulator -= static_cast<float>(s_FixedStepCount) * FixedStep;
	}
}
