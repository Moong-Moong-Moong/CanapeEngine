#pragma once

#include <cstdint>

#include "DX11Simulation/Handles.h"

namespace Canape
{
    class Session;

    struct ParticlesDesc
    {
        // Bytes of one particle.
        std::uint32_t stride = 0;
        std::uint32_t count = 0;
    };

    // An array of fixed-size particles kept across frames, zero at first.
    // A dispatch rewrites it in place and a draw makes one quad per particle.
    class Particles
    {
    private:
        Session& session;
        ParticlesDesc desc;
        BufferHandle buffer;

    public:
        Particles(Session&, const ParticlesDesc&);

        const ParticlesDesc& GetDesc() const
        {
            return desc;
        }

        Buffer& GetBuffer() const;
    };
}
