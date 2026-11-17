#include "DX11Simulation/Kernel.h"

#include <cassert>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/Session.h"

namespace Canape
{
    Kernel::Kernel(
        Session& session,
        const ShaderDesc& shader,
        ConstantView values
    )
        : session(session)
        , shader(session.GetOrCreateComputeShader(shader))
        , constants(session.CreateConstantBuffer(values)) {}

    void Kernel::Upload(ConstantView values)
    {
        auto buffer = session.Get(constants);
        buffer->Upload(values.Data(), values.Size());
    }

    ComputeShader& Kernel::GetShader() const
    {
        auto ptr = session.Get(shader);
        assert(ptr != nullptr);

        return *ptr;
    }

    Buffer& Kernel::GetConstants() const
    {
        auto ptr = session.Get(constants);
        assert(ptr != nullptr);

        return *ptr;
    }
}
