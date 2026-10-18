#include "Math/Random.h"

namespace Canape
{
	Random::Random(uint64_t seed)
	{
		Seed(seed);
	}

	void Random::Seed(uint64_t seed)
	{
		m_State = 0;
		m_Increment = (0xda3e39cb94b95bdbULL << 1u) | 1u;
		NextUInt();
		m_State += seed;
		NextUInt();
	}

	uint32_t Random::NextUInt()
	{
		const uint64_t oldState = m_State;
		m_State = oldState * 6364136223846793005ULL + m_Increment;
		const uint32_t xorShifted = static_cast<uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
		const uint32_t rotation = static_cast<uint32_t>(oldState >> 59u);
		return (xorShifted >> rotation) | (xorShifted << ((32u - rotation) & 31u));
	}

	float Random::NextFloat()
	{
		return static_cast<float>(NextUInt() >> 8) * (1.0f / 16777216.0f);
	}

	bool Random::NextBool()
	{
		return (NextUInt() & 1u) != 0;
	}

	float Random::Range(float min, float max)
	{
		return min + (max - min) * NextFloat();
	}

	int32_t Random::Range(int32_t min, int32_t max)
	{
		if (min >= max)
		{
			return min;
		}

		const uint64_t span = static_cast<uint64_t>(static_cast<int64_t>(max) - static_cast<int64_t>(min)) + 1u;
		const uint64_t value = (static_cast<uint64_t>(NextUInt()) * span) >> 32u;
		return static_cast<int32_t>(static_cast<int64_t>(min) + static_cast<int64_t>(value));
	}

	Vector2 Random::OnUnitCircle()
	{
		const float angle = Range(0.0f, Math::TwoPi);
		return { Math::Cos(angle), Math::Sin(angle) };
	}

	Vector2 Random::InsideUnitCircle()
	{
		return OnUnitCircle() * Math::Sqrt(NextFloat());
	}

	Vector3 Random::OnUnitSphere()
	{
		const float z = Range(-1.0f, 1.0f);
		const float angle = Range(0.0f, Math::TwoPi);
		const float radius = Math::Sqrt(Math::Max(0.0f, 1.0f - z * z));
		return { radius * Math::Cos(angle), radius * Math::Sin(angle), z };
	}

	Vector3 Random::InsideUnitSphere()
	{
		return OnUnitSphere() * Math::Pow(NextFloat(), 1.0f / 3.0f);
	}

	Quaternion Random::Rotation()
	{
		const float u1 = NextFloat();
		const float u2 = NextFloat() * Math::TwoPi;
		const float u3 = NextFloat() * Math::TwoPi;
		const float a = Math::Sqrt(1.0f - u1);
		const float b = Math::Sqrt(u1);
		return { a * Math::Sin(u2), a * Math::Cos(u2), b * Math::Sin(u3), b * Math::Cos(u3) };
	}
}
