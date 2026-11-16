#pragma once

#include "DX11Simulation/ConstantView.h"
#include "DX11Simulation/Handles.h"
#include "DX11Simulation/ShaderDesc.h"

namespace Canape
{
    class Session;

    // The body of a draw
    class Material
    {
    private:
        VertexShaderHandle vertexShader;
        PixelShaderHandle pixelShader;

        BufferHandle constants;

    public:
        Material(
            Session&,
            const ShaderDesc& vertexShader,
            const ShaderDesc& pixelShader,
            ConstantView values
        );

        void Upload(Session&, ConstantView values);

        VertexShaderHandle GetVertexShader() const
        {
            return vertexShader;
        }

        PixelShaderHandle GetPixelShader() const
        {
            return pixelShader;
        }

        BufferHandle GetConstants() const
        {
            return constants;
        }
    };
}
