#include "DX11Simulation/NamedConstants.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <stdexcept>

#include "DX11Simulation/Shader.h"

namespace Canape
{
    std::vector<std::byte> packConstants(const ConstantLayout& layout, NamedConstants values)
    {
        std::vector<std::byte> bytes(layout.size);
        for(const auto& field : layout.fields)
        {
            const auto value = std::ranges::find(values, field.name, &NamedConstant::name);
            if(value == values.end())
            {
                throw std::invalid_argument(
                    std::format("constant '{}' has no value", field.name)
                );
            }

            const auto valueBytes = std::as_bytes(value->values);
            if(valueBytes.size() != field.size)
            {
                throw std::invalid_argument(
                    std::format(
                        "constant '{}' takes {} bytes, its value has {}",
                        field.name,
                        field.size,
                        valueBytes.size()
                    )
                );
            }

            std::memcpy(
                bytes.data() + field.offset,
                valueBytes.data(),
                field.size
            );
        }

        return bytes;
    }
}
