#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace Canape
{
    struct ConstantLayout;

    // A value for the variable of that name in a shader's constant block.
    struct NamedConstant
    {
        std::string_view name;
        std::span<const float> values;
    };

    using NamedConstants = std::span<const NamedConstant>;

    // The bytes of the block: every variable the layout declares takes the value of its name.
    std::vector<std::byte> packConstants(const ConstantLayout&, NamedConstants);
}
