#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace Canape
{
    // A struct of the caller's, seen as the bytes of a constant buffer.
    class ConstantView
    {
    private:
        const void* data = nullptr;
        std::uint32_t size = 0;

    public:
        ConstantView() = default;

        template<typename T>
            requires (!std::same_as<T, ConstantView>)
                && std::is_trivially_copyable_v<T>
                && (sizeof(T) % 16 == 0)
        ConstantView(const T& values)
            : data(&values), size(sizeof(T)) {}

        const void* Data() const
        {
            return data;
        }

        std::uint32_t Size() const
        {
            return size;
        }
    };
}
