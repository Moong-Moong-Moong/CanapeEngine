#pragma once

#include <cstdint>

#include "DX11Simulation/Handles.h"

namespace Canape
{
    class Session;

    // Registers of the constant blocks a draw's shaders read.
    inline constexpr std::uint32_t OuterSlot = 9; // what the world decides.
    inline constexpr std::uint32_t GeometrySlot = 10; // what is drawn.
    inline constexpr std::uint32_t MaterialSlot = 11; // how it is shaded.

    class RenderPass
    {
    private:
        RasterizerStateHandle rasterizer;
        DepthStencilStateHandle depthStencil;
        BlendStateHandle blend;

    public:
        static RenderPass Opaque(Session&);

        RasterizerStateHandle GetRasterizer() const
        {
            return rasterizer;
        }

        DepthStencilStateHandle GetDepthStencil() const
        {
            return depthStencil;
        }

        BlendStateHandle GetBlend() const
        {
            return blend;
        }

    private:
        RenderPass(
            RasterizerStateHandle rasterizer,
            DepthStencilStateHandle depthStencil,
            BlendStateHandle blend
        )
            : rasterizer(rasterizer), depthStencil(depthStencil), blend(blend) {}
    };
}
