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
    // Register of the field a draw reads in its vertex shader.
    inline constexpr std::uint32_t FieldSlot = 8;
    // A draw reads a field or particles, never both.
    inline constexpr std::uint32_t ParticlesSlot = FieldSlot;

    class RenderPass
    {
    private:
        Session& session;
        RasterizerStateHandle rasterizer;
        DepthStencilStateHandle depthStencil;
        BlendStateHandle blendState;

    public:
        static RenderPass Opaque(Session&);
        // Adds to what is drawn behind it; tested against the depth, which it leaves as it is.
        static RenderPass Additive(Session&);

        ID3D11RasterizerState* GetRasterizer() const;
        ID3D11DepthStencilState* GetDepthStencil() const;
        ID3D11BlendState* GetBlend() const;

    private:
        RenderPass(
            Session& session,
            RasterizerStateHandle rasterizer,
            DepthStencilStateHandle depthStencil,
            BlendStateHandle blendState
        )
            : session(session)
            , rasterizer(rasterizer)
            , depthStencil(depthStencil)
            , blendState(blendState) {}
    };
}
