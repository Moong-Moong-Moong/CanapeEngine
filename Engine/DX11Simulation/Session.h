#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/ConstantView.h"
#include "DX11Simulation/D3D.h"
#include "DX11Simulation/FlyweightPool.h"
#include "DX11Simulation/Handles.h"
#include "DX11Simulation/Shader.h"
#include "DX11Simulation/ShaderDesc.h"
#include "DX11Simulation/SlotMap.h"
#include "DX11Simulation/Texture.h"

namespace Canape
{
    class Device;

    // The simulation's share of the renderer's D3D11 device.
    // It owns every resource created from it, until it dies,
    // so it must outlive whatever holds a handle of its.
    class Session
    {
    private:
        template<typename Desc, typename T>
        using StatePool = FlyweightPool<Desc, T, DescHash, DescEqual>;

    private:
        std::unique_ptr<Device> device;

        SlotMap<Buffer> buffers;
        SlotMap<Texture> textures;
        StatePool<D3D11_SAMPLER_DESC, SamplerStateRef> samplerStates;

        StatePool<D3D11_RASTERIZER_DESC, RasterizerStateRef> rasterizerStates;
        StatePool<D3D11_DEPTH_STENCIL_DESC, DepthStencilStateRef> depthStencilStates;
        StatePool<D3D11_BLEND_DESC, BlendStateRef> blendStates;

        FlyweightPool<ShaderDesc, VertexShader> vertexShaders;
        FlyweightPool<ShaderDesc, PixelShader> pixelShaders;
        FlyweightPool<ShaderDesc, ComputeShader> computeShaders;

    public:
        Session(ID3D11Device&, ID3D11DeviceContext&);
        ~Session();

        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
        Session(Session&&) = delete;
        Session& operator=(Session&&) = delete;

        // Each throws as the Device's Create does; what it makes is kept until the session dies.
        [[nodiscard]] BufferHandle CreateBuffer(
            const D3D11_BUFFER_DESC&,
            const D3D11_SUBRESOURCE_DATA* initialData = nullptr,
            std::string_view name = {}
        );
        // Starts with values.
        [[nodiscard]] BufferHandle CreateConstantBuffer(
            ConstantView values,
            std::string_view name = {})
        {
            const D3D11_SUBRESOURCE_DATA initialData{
                .pSysMem = values.Data()
            };
            return CreateBuffer(
                ConstantBufferDesc(values.Size()),
                &initialData,
                name
            );
        }
        // Starts with bytes, which are as long as the buffer.
        [[nodiscard]] BufferHandle CreateConstantBuffer(
            std::span<const std::byte> bytes,
            std::string_view name = {})
        {
            const D3D11_SUBRESOURCE_DATA initialData{
                .pSysMem = bytes.data()
            };
            return CreateBuffer(
                ConstantBufferDesc(static_cast<std::uint32_t>(bytes.size())),
                &initialData,
                name
            );
        }
        [[nodiscard]] TextureHandle CreateTexture(
            const D3D11_TEXTURE2D_DESC&,
            const D3D11_SUBRESOURCE_DATA* initialData = nullptr,
            std::string_view name = {}
        );

        // Equal arguments give the same handle. Each throws as the Device's Create does.
        [[nodiscard]] SamplerStateHandle GetOrCreateSamplerState(const D3D11_SAMPLER_DESC&);
        [[nodiscard]] RasterizerStateHandle GetOrCreateRasterizerState(const D3D11_RASTERIZER_DESC&);
        [[nodiscard]] DepthStencilStateHandle GetOrCreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC&);
        [[nodiscard]] BlendStateHandle GetOrCreateBlendState(const D3D11_BLEND_DESC&);

        [[nodiscard]] VertexShaderHandle GetOrCreateVertexShader(const ShaderDesc&);
        [[nodiscard]] PixelShaderHandle GetOrCreatePixelShader(const ShaderDesc&);
        [[nodiscard]] ComputeShaderHandle GetOrCreateComputeShader(const ShaderDesc&);

        // nullptr for a handle that names nothing.
        template<typename T>
        T* Get(SlotHandle<T> handle)
        {
            return slotsOf<T>().Find(handle);
        }

    private:
        template<typename T>
        auto& slotsOf()
        {
            if constexpr(std::is_same_v<T, Buffer>) return buffers;
            else if constexpr(std::is_same_v<T, Texture>) return textures;
            else if constexpr(std::is_same_v<T, SamplerStateRef>) return samplerStates;
            else if constexpr(std::is_same_v<T, RasterizerStateRef>) return rasterizerStates;
            else if constexpr(std::is_same_v<T, DepthStencilStateRef>) return depthStencilStates;
            else if constexpr(std::is_same_v<T, BlendStateRef>) return blendStates;
            else if constexpr(std::is_same_v<T, VertexShader>) return vertexShaders;
            else if constexpr(std::is_same_v<T, PixelShader>) return pixelShaders;
            else if constexpr(std::is_same_v<T, ComputeShader>) return computeShaders;
            else static_assert(!sizeof(T), "the session keeps no such resource");
        }
    };
}
