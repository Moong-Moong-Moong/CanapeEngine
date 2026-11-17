#include "DX11Simulation/D3D.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace
{
    std::string fromUTF16String(std::wstring_view utf16Str)
    {
        const auto length = WideCharToMultiByte(
            CP_UTF8,
            0,
            utf16Str.data(),
            static_cast<int>(utf16Str.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );
        std::string utf8Str(length, '\0');

        WideCharToMultiByte(
            CP_UTF8,
            0,
            utf16Str.data(),
            static_cast<int>(utf16Str.size()),
            utf8Str.data(),
            length,
            nullptr,
            nullptr
        );

        return utf8Str;
    }

    std::string HResultToString(HRESULT result)
    {
        wchar_t* message = nullptr;
        const auto length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER
                | FORMAT_MESSAGE_FROM_SYSTEM
                | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            static_cast<DWORD>(result),
            0,
            reinterpret_cast<wchar_t*>(&message),
            0,
            nullptr
        );
        if(length == 0)
            return std::format("HRESULT 0x{:08X}", static_cast<std::uint32_t>(result));

        auto text = fromUTF16String(std::wstring_view(message, length));
        LocalFree(message);
        while(!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();

        return text;
    }
}

namespace Canape::detail
{
    void ThrowHResult(HRESULT result, std::string_view msg)
    {
        throw std::runtime_error(
            std::format("{}: {}",
                msg,
                HResultToString(result)
            )
        );
    }

    void ThrowHResult(HRESULT result, std::string_view msg, std::string_view detail)
    {
        if(!detail.empty())
        {
            throw std::runtime_error(
                std::format("{}: {}\n{}",
                    msg,
                    HResultToString(result),
                    detail
                )
            );
        }
        else
        {
            ThrowHResult(result, msg);
        }
    }
}

namespace
{
    template<typename... Fields>
    std::size_t hashOf(const std::tuple<const Fields&...>& fields)
    {
        return std::apply(
            [](const Fields&... field)
            {
                std::size_t hash = 0;
                ((hash = hash * 31 + std::hash<Fields>{}(field)), ...);

                return hash;
            },
            fields
        );
    }

    auto fieldsOf(const D3D11_SAMPLER_DESC& desc)
    {
        return std::tie(
            desc.Filter,
            desc.AddressU,
            desc.AddressV,
            desc.AddressW,
            desc.MipLODBias,
            desc.MaxAnisotropy,
            desc.ComparisonFunc,
            desc.BorderColor[0],
            desc.BorderColor[1],
            desc.BorderColor[2],
            desc.BorderColor[3],
            desc.MinLOD,
            desc.MaxLOD
        );
    }

    auto fieldsOf(const D3D11_RASTERIZER_DESC& desc)
    {
        return std::tie(
            desc.FillMode,
            desc.CullMode,
            desc.FrontCounterClockwise,
            desc.DepthBias,
            desc.DepthBiasClamp,
            desc.SlopeScaledDepthBias,
            desc.DepthClipEnable,
            desc.ScissorEnable,
            desc.MultisampleEnable,
            desc.AntialiasedLineEnable
        );
    }

    auto fieldsOf(const D3D11_DEPTH_STENCILOP_DESC& desc)
    {
        return std::tie(
            desc.StencilFailOp,
            desc.StencilDepthFailOp,
            desc.StencilPassOp,
            desc.StencilFunc
        );
    }

    auto fieldsOf(const D3D11_DEPTH_STENCIL_DESC& desc)
    {
        return std::tuple_cat(
            std::tie(
                desc.DepthEnable,
                desc.DepthWriteMask,
                desc.DepthFunc,
                desc.StencilEnable,
                desc.StencilReadMask,
                desc.StencilWriteMask
            ),
            fieldsOf(desc.FrontFace),
            fieldsOf(desc.BackFace)
        );
    }

    auto fieldsOf(const D3D11_RENDER_TARGET_BLEND_DESC& desc)
    {
        return std::tie(
            desc.BlendEnable,
            desc.SrcBlend,
            desc.DestBlend,
            desc.BlendOp,
            desc.SrcBlendAlpha,
            desc.DestBlendAlpha,
            desc.BlendOpAlpha,
            desc.RenderTargetWriteMask
        );
    }

    auto fieldsOf(const D3D11_BLEND_DESC& desc)
    {
        return std::tuple_cat(
            std::tie(desc.AlphaToCoverageEnable, desc.IndependentBlendEnable),
            fieldsOf(desc.RenderTarget[0]),
            fieldsOf(desc.RenderTarget[1]),
            fieldsOf(desc.RenderTarget[2]),
            fieldsOf(desc.RenderTarget[3]),
            fieldsOf(desc.RenderTarget[4]),
            fieldsOf(desc.RenderTarget[5]),
            fieldsOf(desc.RenderTarget[6]),
            fieldsOf(desc.RenderTarget[7])
        );
    }
}

namespace Canape
{
    std::size_t DescHash::operator()(const D3D11_SAMPLER_DESC& desc) noexcept
    {
        return hashOf(fieldsOf(desc));
    }

    std::size_t DescHash::operator()(const D3D11_RASTERIZER_DESC& desc) noexcept
    {
        return hashOf(fieldsOf(desc));
    }

    std::size_t DescHash::operator()(const D3D11_DEPTH_STENCIL_DESC& desc) noexcept
    {
        return hashOf(fieldsOf(desc));
    }

    std::size_t DescHash::operator()(const D3D11_BLEND_DESC& desc) noexcept
    {
        return hashOf(fieldsOf(desc));
    }

    bool DescEqual::operator()(const D3D11_SAMPLER_DESC& a, const D3D11_SAMPLER_DESC& b) noexcept
    {
        return fieldsOf(a) == fieldsOf(b);
    }

    bool DescEqual::operator()(const D3D11_RASTERIZER_DESC& a, const D3D11_RASTERIZER_DESC& b) noexcept
    {
        return fieldsOf(a) == fieldsOf(b);
    }

    bool DescEqual::operator()(const D3D11_DEPTH_STENCIL_DESC& a, const D3D11_DEPTH_STENCIL_DESC& b) noexcept
    {
        return fieldsOf(a) == fieldsOf(b);
    }

    bool DescEqual::operator()(const D3D11_BLEND_DESC& a, const D3D11_BLEND_DESC& b) noexcept
    {
        return fieldsOf(a) == fieldsOf(b);
    }
}
