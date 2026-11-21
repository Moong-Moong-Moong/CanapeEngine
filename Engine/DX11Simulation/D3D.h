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
        static std::size_t operator()(const D3D11_SAMPLER_DESC&) noexcept;
        static std::size_t operator()(const D3D11_RASTERIZER_DESC&) noexcept;
        static std::size_t operator()(const D3D11_DEPTH_STENCIL_DESC&) noexcept;
        static std::size_t operator()(const D3D11_BLEND_DESC&) noexcept;
    };

    struct DescEqual
    {
        static bool operator()(const D3D11_SAMPLER_DESC&, const D3D11_SAMPLER_DESC&) noexcept;
        static bool operator()(const D3D11_RASTERIZER_DESC&, const D3D11_RASTERIZER_DESC&) noexcept;
        static bool operator()(const D3D11_DEPTH_STENCIL_DESC&, const D3D11_DEPTH_STENCIL_DESC&) noexcept;
        static bool operator()(const D3D11_BLEND_DESC&, const D3D11_BLEND_DESC&) noexcept;
    };

    void SetDebugName(ID3D11DeviceChild&, std::string_view);
}
