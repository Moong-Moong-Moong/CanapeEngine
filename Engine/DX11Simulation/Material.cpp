#include "DX11Simulation/Material.h"

#include <cassert>
#include <stdexcept>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/RenderPass.h"
#include "DX11Simulation/Session.h"

namespace Canape
{
    namespace
    {
        // Both stages read the same block, so either's declaration stands for it.
        const ConstantLayout& constantLayoutOf(
            const VertexShader& vertexShader,
            const PixelShader& pixelShader
        )
        {
            if(const auto* layout = vertexShader.FindConstantLayout(MaterialSlot)) return *layout;
            if(const auto* layout = pixelShader.FindConstantLayout(MaterialSlot)) return *layout;

            throw std::invalid_argument("the material's shaders declare no constant block");
        }
    }

    Material::Material(
        Session& session,
        const ShaderDesc& vertexShader,
        const ShaderDesc& pixelShader,
        ConstantView values
    )
        : session(session)
        , vertexShader(session.GetOrCreateVertexShader(vertexShader))
        , pixelShader(session.GetOrCreatePixelShader(pixelShader))
        , constants(session.CreateConstantBuffer(values)) {}

    Material::Material(
        Session& session,
        const ShaderDesc& vertexShader,
        const ShaderDesc& pixelShader,
        NamedConstants values
    )
        : session(session)
        , vertexShader(session.GetOrCreateVertexShader(vertexShader))
        , pixelShader(session.GetOrCreatePixelShader(pixelShader))
        , constants(
            session.CreateConstantBuffer(
                packConstants(
                    constantLayoutOf(
                        *session.Get(this->vertexShader),
                        *session.Get(this->pixelShader)
                    ),
                    values
                )
            )
        ) {}

    void Material::Upload(ConstantView values)
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
