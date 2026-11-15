#pragma once

#include <string_view>

#include <d3d11.h>
#include <wrl/client.h>

namespace Canape
{
    template<typename T>
    using RefPtr = Microsoft::WRL::ComPtr<T>;

    using DeviceRef = RefPtr<ID3D11Device>;
    using DeviceContextRef = RefPtr<ID3D11DeviceContext>;
    
    using BufferRef = RefPtr<ID3D11Buffer>;
    using Texture2DRef = RefPtr<ID3D11Texture2D>;
    using ReadViewRef = RefPtr<ID3D11ShaderResourceView>;
    using WriteViewRef = RefPtr<ID3D11UnorderedAccessView>;
    using SamplerStateRef = RefPtr<ID3D11SamplerState>;

    using RasterizerStateRef = RefPtr<ID3D11RasterizerState>;
    using DepthStencilStateRef = RefPtr<ID3D11DepthStencilState>;
    using BlendStateRef = RefPtr<ID3D11BlendState>;

    using VertexShaderRef = RefPtr<ID3D11VertexShader>;
    using PixelShaderRef = RefPtr<ID3D11PixelShader>;
    using ComputeShaderRef = RefPtr<ID3D11ComputeShader>;

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
}
