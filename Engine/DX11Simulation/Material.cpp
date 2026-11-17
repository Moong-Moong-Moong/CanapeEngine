#include "DX11Simulation/Material.h"

#include <cassert>

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
        : session(session)
        , vertexShader(session.GetOrCreateVertexShader(vertexShader))
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

    ID3D11VertexShader* Material::GetVertexShader() const
    {
        auto ptr = session.Get(vertexShader);
        assert(ptr != nullptr);

        return ptr->Get();
    }

    ID3D11PixelShader* Material::GetPixelShader() const
    {
        auto ptr = session.Get(pixelShader);
        assert(ptr != nullptr);

        return ptr->Get();
    }

    Buffer& Material::GetConstants() const
    {
        auto ptr = session.Get(constants);
        assert(ptr != nullptr);

        return *ptr;
    }
}
