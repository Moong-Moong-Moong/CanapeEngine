#include "DX11Simulation/Geometry.h"

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/Session.h"

namespace Canape
{
    Geometry::Geometry(Session& session, ConstantView values, std::uint32_t vertexCount)
        : constants(session.CreateBuffer(ConstantBufferDesc(values.Size()))),
          vertexCount(vertexCount)
    {
        Upload(session, values);
    }

    void Geometry::Upload(Session& session, ConstantView values)
    {
        auto buffer = session.Get(constants);
        buffer->Upload(values.Data(), values.Size());
    }
}
