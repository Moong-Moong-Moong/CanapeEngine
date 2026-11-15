#pragma once

#include <filesystem>
#include <string>

namespace Canape
{
    struct ShaderDesc {
        // Relative to the executable unless absolute.
        std::filesystem::path path;
        std::string entryPoint;
    };
}
