#pragma once

#include <memory>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/ConstantView.h"
#include "DX11Simulation/D3D.h"
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
        template<typename Desc, typename Handle>
        using HandlesByDesc = std::unordered_map<Desc, Handle, DescHash, DescEqual>;

    private:
        std::unique_ptr<Device> device;

        SlotMap<Buffer> buffers;
        SlotMap<Texture> textures;
        SlotMap<SamplerStateRef> samplerStates;
        HandlesByDesc<D3D11_SAMPLER_DESC, SamplerStateHandle> samplerStateHandles;

        SlotMap<RasterizerStateRef> rasterizerStates;
        HandlesByDesc<D3D11_RASTERIZER_DESC, RasterizerStateHandle> rasterizerStateHandles;
        SlotMap<DepthStencilStateRef> depthStencilStates;
        HandlesByDesc<D3D11_DEPTH_STENCIL_DESC, DepthStencilStateHandle> depthStencilStateHandles;
        SlotMap<BlendStateRef> blendStates;
        HandlesByDesc<D3D11_BLEND_DESC, BlendStateHandle> blendStateHandles;

        SlotMap<VertexShaderRef> vertexShaders;
        std::unordered_map<ShaderDesc, VertexShaderHandle> vertexShaderHandles;
        SlotMap<PixelShaderRef> pixelShaders;
        std::unordered_map<ShaderDesc, PixelShaderHandle> pixelShaderHandles;
        SlotMap<ComputeShader> computeShaders;
        std::unordered_map<ShaderDesc, ComputeShaderHandle> computeShaderHandles;

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
            return slotsOf<T>().Get(handle);
        }

    private:
        template<typename T>
        SlotMap<T>& slotsOf()
        {
            if constexpr(std::is_same_v<T, Buffer>) return buffers;
            else if constexpr(std::is_same_v<T, Texture>) return textures;
            else if constexpr(std::is_same_v<T, SamplerStateRef>) return samplerStates;
            else if constexpr(std::is_same_v<T, RasterizerStateRef>) return rasterizerStates;
            else if constexpr(std::is_same_v<T, DepthStencilStateRef>) return depthStencilStates;
            else if constexpr(std::is_same_v<T, BlendStateRef>) return blendStates;
            else if constexpr(std::is_same_v<T, VertexShaderRef>) return vertexShaders;
            else if constexpr(std::is_same_v<T, PixelShaderRef>) return pixelShaders;
            else if constexpr(std::is_same_v<T, ComputeShader>) return computeShaders;
            else static_assert(!sizeof(T), "the session keeps no such resource");
        }
    };
}
