#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <Engine.h>

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

// The components of type T on the node's object and on every object below it, depth first.
template<typename T>
void collectBelow(const Canape::TransformComponent& node, std::vector<T*>& found)
{
    if(auto* component = node.GetOwnerObject()->GetComponent<T>())
    {
        found.push_back(component);
    }

    for(const auto* child : node.GetChildren())
    {
        collectBelow(*child, found);
    }
}
