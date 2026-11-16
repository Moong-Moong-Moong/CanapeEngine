#include "DX11Simulation/RenderPass.h"

#include "DX11Simulation/D3D.h"
#include "DX11Simulation/Session.h"

namespace Canape
{
    namespace
    {
        constexpr D3D11_RASTERIZER_DESC NoCullRasterizerDesc {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
            .DepthClipEnable = TRUE
        };

        constexpr D3D11_DEPTH_STENCIL_DESC OpaqueDepthStencilDesc {
            .DepthEnable = TRUE,
            .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
            .DepthFunc = D3D11_COMPARISON_LESS
        };

        constexpr D3D11_BLEND_DESC OpaqueBlendDesc {
            .RenderTarget = { {
                .BlendEnable = FALSE,
                .SrcBlend = D3D11_BLEND_ONE,
                .DestBlend = D3D11_BLEND_ZERO,
                .BlendOp = D3D11_BLEND_OP_ADD,
                .SrcBlendAlpha = D3D11_BLEND_ONE,
                .DestBlendAlpha = D3D11_BLEND_ZERO,
                .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL
            } }
        };
    }

    RenderPass RenderPass::Opaque(Session& session)
    {
        return {
            session.GetOrCreateRasterizerState(NoCullRasterizerDesc),
            session.GetOrCreateDepthStencilState(OpaqueDepthStencilDesc),
            session.GetOrCreateBlendState(OpaqueBlendDesc)
        };
    }
}
