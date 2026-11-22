#include "DX11Simulation/Geometry.h"

#include <cassert>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/Session.h"

namespace Canape
{
    Geometry::Geometry(Session& session, ConstantView values, std::uint32_t vertexCount)
        : session(session)
        , constants(session.CreateConstantBuffer(values))
        , vertexCount(vertexCount) {}

    void Geometry::Upload(ConstantView values)
    {
        auto buffer = session.Get(constants);
        buffer->Upload(values.Data(), values.Size());
    }

    Buffer& Geometry::GetConstants() const
    {
        auto ptr = session.Get(constants);
        assert(ptr != nullptr);

        return *ptr;
    }
}
