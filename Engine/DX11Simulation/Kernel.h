#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "DX11Simulation/ConstantView.h"
#include "DX11Simulation/Handles.h"
#include "DX11Simulation/NamedConstants.h"
#include "DX11Simulation/ShaderDesc.h"

namespace Canape
{
    class Session;

    // A 2D grid of authored values a kernel reads
    struct MapTexels
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        // The rows from the top, each cell as four channels
        std::span<const std::byte> texels;
    };

    // The body of a dispatch
    class Kernel
    {
    private:
        Session& session;

        ComputeShaderHandle shader;

        BufferHandle constants;
        TextureHandle map;

    public:
        Kernel(
            Session&,
            const ShaderDesc& shader,
            ConstantView values
        );
        // Fills the constant block the shader declares
        Kernel(
            Session&,
            const ShaderDesc& shader,
            NamedConstants values,
            std::optional<MapTexels> map
        );

        void Upload(ConstantView values);
        // As the constructor: every variable the block declares is given again.
        void Upload(NamedConstants values);

        ComputeShader& GetShader() const;
        Buffer& GetConstants() const;
        Texture* GetMap() const;
    };
}
