#pragma once

#include "DX11Simulation/ConstantView.h"
#include "DX11Simulation/Handles.h"
#include "DX11Simulation/NamedConstants.h"
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
        // Fills the constant block the shader declares
        Kernel(
            Session&,
            const ShaderDesc& shader,
            NamedConstants values
        );

        void Upload(ConstantView values);
        // As the constructor: every variable the block declares is given again.
        void Upload(NamedConstants values);

        ComputeShader& GetShader() const;
        Buffer& GetConstants() const;
    };
}
