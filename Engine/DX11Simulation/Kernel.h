#pragma once

#include "DX11Simulation/ConstantView.h"
#include "DX11Simulation/Handles.h"
#include "DX11Simulation/ShaderDesc.h"

namespace Canape
{
    class Session;

    // The body of a dispatch
    class Kernel
    {
    private:
        Session& session;

        ComputeShaderHandle shader;

        BufferHandle constants;

    public:
        Kernel(
            Session&,
            const ShaderDesc& shader,
            ConstantView values
        );

        void Upload(ConstantView values);

        ComputeShader& GetShader() const;
        Buffer& GetConstants() const;
    };
}
