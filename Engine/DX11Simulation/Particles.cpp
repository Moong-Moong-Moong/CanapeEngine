#include "DX11Simulation/Particles.h"

#include <cassert>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/Session.h"

namespace Canape
{
    Particles::Particles(Session& session, const ParticlesDesc& desc)
        : session(session), desc(desc)
        , buffer(session.CreateBuffer(StructuredBufferDesc(desc.stride, desc.count))) {}

    Buffer& Particles::GetBuffer() const
    {
        auto ptr = session.Get(buffer);
        assert(ptr != nullptr);

        return *ptr;
    }
}
