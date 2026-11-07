#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <Engine.h>

using GameObjectHandles = std::vector<Handle<Canape::GameObject>>;

enum class PuzzleKind : std::uint8_t {
    KickerJump,
    TiltTray,
    CarriedScoop,
};

// Solution runs the intended moves; Control runs the same bodies without the decisive move.
enum class PuzzleMode : std::uint8_t {
    Solution,
    Control,
};

inline constexpr std::uint32_t PuzzleHorizonTicks = 600;
inline constexpr std::uint32_t PuzzleRestTicks = 60;

// The name the enumerator was registered with; empty for a value that was not.
// TODO. move to engine core?
template<typename E>
    requires std::is_enum_v<E>
std::string enumName(E value)
{
    return rttr::type::get<E>().get_enumeration().value_to_name(value).to_string();
}
