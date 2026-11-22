#include "DX11Simulation/Kernel.h"

#include <cassert>
#include <stdexcept>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/ComputePass.h"
#include "DX11Simulation/D3D.h"
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

        TextureHandle createMap(Session& session, const MapTexels& map)
        {
            constexpr std::uint32_t BytesPerTexel = 4;

            const UINT memPitch = BytesPerTexel * map.width;
            assert(map.texels.size() == memPitch * map.height);

            const D3D11_TEXTURE2D_DESC textureDesc{
                .Width = map.width,
                .Height = map.height,
                .MipLevels = 1,
                .ArraySize = 1,
                .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                .SampleDesc = { .Count = 1 },
                .Usage = D3D11_USAGE_IMMUTABLE,
                .BindFlags = D3D11_BIND_SHADER_RESOURCE
            };
            const D3D11_SUBRESOURCE_DATA initialData{
                .pSysMem = map.texels.data(),
                .SysMemPitch = memPitch
            };
            return session.CreateTexture(textureDesc, &initialData);
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
        NamedConstants values,
        std::optional<MapTexels> map
    )
        : session(session)
        , shader(session.GetOrCreateComputeShader(shader))
        , constants(
            session.CreateConstantBuffer(
                packConstants(constantLayoutOf(GetShader()), values)
            )
        )
        , map(map.has_value() ?
            createMap(session, *map) :
            TextureHandle{}
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

    Texture* Kernel::GetMap() const
    {
        return session.Get(map);
    }
}
