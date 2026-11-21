#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

namespace Canape
{
    struct ShaderDesc
    {
        // Relative to the executable unless absolute.
        std::filesystem::path path;
        std::string entryPoint;

        bool operator==(const ShaderDesc&) const = default;
    };
}

template<>
struct std::hash<Canape::ShaderDesc>
{
    static std::size_t operator()(const Canape::ShaderDesc& desc) noexcept
    {
        const auto pathHash = std::filesystem::hash_value(desc.path);
        const auto entryPointHash = std::hash<std::string>{}(desc.entryPoint);

        return pathHash ^ (entryPointHash << 1);
    }
};
