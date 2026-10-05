#pragma once

#include <chrono>

namespace Canape
{
	class Time
	{
	public:
		using Clock = std::chrono::steady_clock;

		static float DeltaTime() { return s_DeltaTime; }

	private:
		friend class Application;

		static void Reset();
		static void Tick();

		static Clock::time_point s_BaseTime;
		static Clock::time_point s_PrevTime;

		static float s_DeltaTime;
	};
}
