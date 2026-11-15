#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "DX11Simulation/D3D.h"
#include "DX11Simulation/ShaderDesc.h"

namespace Canape
{
    using ThreadGroupSize = std::array<std::uint32_t, 3>;

    // path is relative to the executable unless absolute. Both throw std::runtime_error carrying the compiler output.
    VertexShaderRef CompileVertexShader(
        ID3D11Device&,
        const ShaderDesc&,
        std::string_view name = {}
    );
    PixelShaderRef CompilePixelShader(
        ID3D11Device&,
        const ShaderDesc&,
        std::string_view name = {}
    );

    // A compute shader and the numthreads it was compiled with.
    class ComputeShader
    {
    private:
        ComputeShaderRef shader;
        ThreadGroupSize threadGroupSize{};

    public:
        // path and what it throws are as for CompileVertexShader.
        ComputeShader(
            ID3D11Device&,
            const ShaderDesc&,
            std::string_view name = {}
        );

        const ThreadGroupSize& GetThreadGroupSize() const
        {
            return threadGroupSize;
        }

        ID3D11ComputeShader* Get() const
        {
            return shader.Get();
        }
    };
}
