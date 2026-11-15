#pragma once

#include <memory>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/D3D.h"
#include "DX11Simulation/Shader.h"
#include "DX11Simulation/SlotMap.h"
#include "DX11Simulation/Texture.h"

namespace Canape
{
    class Device;

    // The simulation's share of the renderer's D3D11 device.
    // It owns every resource created from it, until it dies,
    // so it must outlive whatever holds a handle of its.
    class Session {
    private:
        std::unique_ptr<Device> device;

        SlotMap<Buffer> buffers;
        SlotMap<Texture> textures;
        SlotMap<SamplerStateRef> samplerStates;

        SlotMap<RasterizerStateRef> rasterizerStates;
        SlotMap<DepthStencilStateRef> depthStencilStates;
        SlotMap<BlendStateRef> blendStates;

        SlotMap<VertexShaderRef> vertexShaders;
        SlotMap<PixelShaderRef> pixelShaders;
        SlotMap<ComputeShader> computeShaders;

    public:
        Session(ID3D11Device&, ID3D11DeviceContext&);
        ~Session();

        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
        Session(Session&&) = delete;
        Session& operator=(Session&&) = delete;

        Device& GetDevice() const
        {
            return *device;
        }
    };
}
