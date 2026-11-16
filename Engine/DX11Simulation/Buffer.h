#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "DX11Simulation/D3D.h"

namespace Canape
{
    // Rewritten whole by the CPU, through Upload.
    inline constexpr D3D11_BUFFER_DESC ConstantBufferDesc(std::uint32_t size)
    {
        return {
            .ByteWidth = size,
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE
        };
    }

    // Read as a StructuredBuffer and written as a RWStructuredBuffer.
    inline constexpr D3D11_BUFFER_DESC StructuredBufferDesc(std::uint32_t stride, std::uint32_t count)
    {
        return {
            .ByteWidth = stride * count,
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
            .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
            .StructureByteStride = stride
        };
    }

    class Buffer
    {
    private:
        BufferRef buffer = nullptr;

        ID3D11DeviceContext& context;

        // view covers every element of a structured buffer
        ReadViewRef shaderResourceView;
        WriteViewRef unorderedAccessView;

    public:
        Buffer(
            ID3D11Device&,
            ID3D11DeviceContext&,
            const D3D11_BUFFER_DESC&,
            const D3D11_SUBRESOURCE_DATA* initialData = nullptr,
            std::string_view name = {}
        );

        // Replaces the content of a dynamic buffer; data is as large as the buffer.
        void Upload(std::span<const std::byte> data);
        void Upload(const void* data, std::size_t size)
        {
            return Upload(std::span{static_cast<const std::byte*>(data), size});
        }
        // Copies the whole buffer out through a staging copy; data is as large as the buffer.
        void Download(std::span<std::byte> data) const;

        D3D11_BUFFER_DESC GetDesc() const;

        ID3D11Buffer* Get() const
        {
            return buffer.Get();
        }

        // Each is null unless the buffer has the matching bind flag.
        ID3D11ShaderResourceView* GetSRV() const
        {
            return shaderResourceView.Get();
        }
        ID3D11UnorderedAccessView* GetUAV() const
        {
            return unorderedAccessView.Get();
        }
    };
}
