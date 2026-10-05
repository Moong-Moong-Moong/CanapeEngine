#pragma once

#include <cstdint>
#include <limits>

namespace Canape
{
	struct GameObjectHandle
	{
		static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

		std::uint32_t Index = InvalidIndex;
		std::uint32_t Generation = 0;

		constexpr bool IsValid() const { return Index != InvalidIndex; }

		constexpr bool operator==(const GameObjectHandle& rhs) const = default;
	};
}
