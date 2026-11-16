#include "DX11Simulation/Material.h"

#include <cstddef>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/Session.h"

namespace Canape
{
    Material::Material(
        Session& session,
        const ShaderDesc& vertexShader,
        const ShaderDesc& pixelShader,
        ConstantView values
    )
        : vertexShader(session.GetOrCreateVertexShader(vertexShader))
        , pixelShader(session.GetOrCreatePixelShader(pixelShader))
        , constants(session.CreateBuffer(ConstantBufferDesc(values.Size())))
    {
        Upload(session, values);
    }

    void Material::Upload(Session& session, ConstantView values)
    {
        auto buffer = session.Get(constants);
        buffer->Upload(values.Data(), values.Size());
    }
}
