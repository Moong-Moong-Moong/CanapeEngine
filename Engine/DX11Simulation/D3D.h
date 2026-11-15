#pragma once

#include <string_view>

#include <d3d11.h>
#include <wrl/client.h>

#include "DX11Simulation/D3DFwd.h"

namespace Canape
{
    namespace detail
    {
        [[noreturn]] void ThrowHResult(HRESULT, std::string_view msg);
        [[noreturn]] void ThrowHResult(HRESULT, std::string_view msg, std::string_view detail);
    }

#define ThrowIfFailed(expr, ...)                                                                   \
    do {                                                                                           \
        if(const HRESULT result = (expr); FAILED(result)) [[unlikely]]   \
        {                                                                                          \
            ::Canape::detail::ThrowHResult(result, __VA_ARGS__);                              \
        }                                                                                          \
    } while(false)

    // By their fields, so that a state's DESC can be the key of a map.
    struct DescHash
    {
        std::size_t operator()(const D3D11_SAMPLER_DESC&) const noexcept;
        std::size_t operator()(const D3D11_RASTERIZER_DESC&) const noexcept;
        std::size_t operator()(const D3D11_DEPTH_STENCIL_DESC&) const noexcept;
        std::size_t operator()(const D3D11_BLEND_DESC&) const noexcept;
    };

    struct DescEqual
    {
        bool operator()(const D3D11_SAMPLER_DESC&, const D3D11_SAMPLER_DESC&) const noexcept;
        bool operator()(const D3D11_RASTERIZER_DESC&, const D3D11_RASTERIZER_DESC&) const noexcept;
        bool operator()(const D3D11_DEPTH_STENCIL_DESC&, const D3D11_DEPTH_STENCIL_DESC&) const noexcept;
        bool operator()(const D3D11_BLEND_DESC&, const D3D11_BLEND_DESC&) const noexcept;
    };
}
