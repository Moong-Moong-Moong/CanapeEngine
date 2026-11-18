#include "DX11Simulation/Kernel.h"

#include <cassert>
#include <stdexcept>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/ComputePass.h"
#include "DX11Simulation/Session.h"

namespace Canape
{
    namespace
    {
        const ConstantLayout& constantLayoutOf(const ComputeShader& shader)
        {
            const auto* layout = shader.FindConstantLayout(KernelSlot);
            if(layout == nullptr)
            {
                throw std::invalid_argument("the kernel's shader declares no constant block");
            }

            return *layout;
        }
    }

    Kernel::Kernel(
        Session& session,
        const ShaderDesc& shader,
        ConstantView values
    )
        : session(session)
        , shader(session.GetOrCreateComputeShader(shader))
        , constants(session.CreateConstantBuffer(values)) {}

    Kernel::Kernel(
        Session& session,
        const ShaderDesc& shader,
        NamedConstants values
    )
        : session(session)
        , shader(session.GetOrCreateComputeShader(shader))
        , constants(
            session.CreateConstantBuffer(
                packConstants(constantLayoutOf(GetShader()), values)
            )
        ) {}

    void Kernel::Upload(ConstantView values)
    {
        auto buffer = session.Get(constants);
        buffer->Upload(values.Data(), values.Size());
    }

    void Kernel::Upload(NamedConstants values)
    {
        GetConstants().Upload(
            packConstants(constantLayoutOf(GetShader()), values)
        );
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
