#pragma once

#include "Math/Quaternion.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"

#include <cstdint>

namespace Canape
{
	class Random
	{
	public:
		explicit Random(uint64_t seed = 0x853c49e6748fea9bULL);

		void Seed(uint64_t seed);

		uint32_t NextUInt();
		float NextFloat();
		bool NextBool();

		float Range(float min, float max);
		int32_t Range(int32_t min, int32_t max);

		Vector2 OnUnitCircle();
		Vector2 InsideUnitCircle();
		Vector3 OnUnitSphere();
		Vector3 InsideUnitSphere();
		Quaternion Rotation();

	private:
		uint64_t m_State = 0;
		uint64_t m_Increment = 0;
	};
}
