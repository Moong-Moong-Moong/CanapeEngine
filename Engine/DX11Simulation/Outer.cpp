#include "DX11Simulation/Outer.h"

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/Session.h"

namespace Canape
{
    Outer::Outer(Session& session, ConstantView values)
        : constants(session.CreateBuffer(ConstantBufferDesc(values.Size())))
    {
        Upload(session, values);
    }

    void Outer::Upload(Session& session, ConstantView values)
    {
        auto buffer = session.Get(constants);
        buffer->Upload(values.Data(), values.Size());
    }
}
