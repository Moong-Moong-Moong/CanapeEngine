#pragma once

#include <chrono>
#include <cstdint>

namespace Canape
{
	class Time
	{
	public:
		using Clock = std::chrono::steady_clock;

		// Scaled by TimeScale.
		static float DeltaTime() { return s_DeltaTime; }
		static float UnscaledDeltaTime() { return s_UnscaledDeltaTime; }
		static constexpr float FixedDeltaTime() { return FixedStep; }
		// Fixed steps the active scene runs this frame.
		static std::uint32_t FixedStepCount() { return s_FixedStepCount; }

		static float TimeScale() { return s_TimeScale; }
		static void SetTimeScale(float timeScale);

	private:
		static constexpr float FixedStep = 1.0f / 60.0f;
		static constexpr std::uint32_t MaxFixedStepsPerFrame = 4;

		friend class Application;

		static void Reset();
		static void Tick();

		static Clock::time_point s_BaseTime;
		static Clock::time_point s_PrevTime;

		static float s_DeltaTime;
		static float s_UnscaledDeltaTime;
		static float s_TimeScale;
		static float s_FixedAccumulator;
		static std::uint32_t s_FixedStepCount;
	};
}
