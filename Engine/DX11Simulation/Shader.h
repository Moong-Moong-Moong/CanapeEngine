#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "DX11Simulation/D3D.h"
#include "DX11Simulation/ShaderDesc.h"

namespace Canape
{
    using ThreadGroupSize = std::array<std::uint32_t, 3>;

    // A variable of a constant block; offset and size in bytes.
    struct ConstantField
    {
        std::string name;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    // What a compiled shader declares in one constant block.
    struct ConstantLayout
    {
        // Bytes of the whole block.
        std::uint32_t size = 0;
        std::vector<ConstantField> fields;
    };

    // Of the constant blocks a shader reads, by register.
    using ConstantLayouts = std::unordered_map<std::uint32_t, ConstantLayout>;

    // path is relative to the executable unless absolute.
    class VertexShader
    {
    private:
        VertexShaderRef shader;
        ConstantLayouts constantLayouts;

    public:
        VertexShader(
            ID3D11Device&,
            const ShaderDesc&,
            std::string_view name = {}
        );

        // nullptr when the shader reads no constant block at that register.
        const ConstantLayout* FindConstantLayout(std::uint32_t slot) const
        {
            const auto found = constantLayouts.find(slot);
            return found != constantLayouts.end() ? &found->second : nullptr;
        }

        ID3D11VertexShader* Get() const
        {
            return shader.Get();
        }
    };

    // path and what it throws are as for VertexShader.
    class PixelShader
    {
    private:
        PixelShaderRef shader;
        ConstantLayouts constantLayouts;

    public:
        PixelShader(
            ID3D11Device&,
            const ShaderDesc&,
            std::string_view name = {}
        );

        // nullptr when the shader reads no constant block at that register.
        const ConstantLayout* FindConstantLayout(std::uint32_t slot) const
        {
            const auto found = constantLayouts.find(slot);
            return found != constantLayouts.end() ? &found->second : nullptr;
        }

        ID3D11PixelShader* Get() const
        {
            return shader.Get();
        }
    };

    // A compute shader and the numthreads it was compiled with.
    class ComputeShader
    {
    private:
        ComputeShaderRef shader;
        ThreadGroupSize threadGroupSize{};
        ConstantLayouts constantLayouts;

    public:
        // path and what it throws are as for VertexShader.
        ComputeShader(
            ID3D11Device&,
            const ShaderDesc&,
            std::string_view name = {}
        );

        const ThreadGroupSize& GetThreadGroupSize() const
        {
            return threadGroupSize;
        }

        // nullptr when the shader reads no constant block at that register.
        const ConstantLayout* FindConstantLayout(std::uint32_t slot) const
        {
            const auto found = constantLayouts.find(slot);
            return found != constantLayouts.end() ? &found->second : nullptr;
        }

        ID3D11ComputeShader* Get() const
        {
            return shader.Get();
        }
    };
}
