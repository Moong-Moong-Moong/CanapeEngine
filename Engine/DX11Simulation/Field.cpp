#include "DX11Simulation/Field.h"

#include <cassert>

#include "DX11Simulation/D3D.h"
#include "DX11Simulation/Session.h"

namespace Canape
{
    namespace
    {
        auto toTextureDesc(const FieldDesc& desc)
        {
            return D3D11_TEXTURE2D_DESC{
                .Width = desc.width,
                .Height = desc.height,
                .MipLevels = 1,
                .ArraySize = 1,
                .Format = DXGI_FORMAT_R32G32_FLOAT,
                .SampleDesc = { .Count = 1 },
                .Usage = D3D11_USAGE_DEFAULT,
                .BindFlags = D3D11_BIND_SHADER_RESOURCE
                    | D3D11_BIND_UNORDERED_ACCESS
            };
        }
    }

    Field::Field(Session& session, const FieldDesc& desc)
        : session(session), desc(desc)
        , latest(session.CreateTexture(toTextureDesc(desc)))
        , next(session.CreateTexture(toTextureDesc(desc))) {}

    Texture& Field::GetLatest() const
    {
        auto ptr = session.Get(latest);
        assert(ptr != nullptr);

        return *ptr;
    }

    Texture& Field::GetNext() const
    {
        auto ptr = session.Get(next);
        assert(ptr != nullptr);

        return *ptr;
    }
}
