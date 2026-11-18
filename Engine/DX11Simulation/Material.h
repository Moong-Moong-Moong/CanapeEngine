#pragma once

#include "DX11Simulation/ConstantView.h"
#include "DX11Simulation/Handles.h"
#include "DX11Simulation/NamedConstants.h"
#include "DX11Simulation/ShaderDesc.h"

namespace Canape
{
    class Session;

    // The body of a draw
    class Material
    {
    private:
        Session& session;

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
        // Fills the constant block the shaders declare
        Material(
            Session&,
            const ShaderDesc& vertexShader,
            const ShaderDesc& pixelShader,
            NamedConstants values
        );

        void Upload(ConstantView values);

        ID3D11VertexShader* GetVertexShader() const;
        ID3D11PixelShader* GetPixelShader() const;
        Buffer& GetConstants() const;
    };
}
