#pragma once

#include "DX11Simulation/D3DFwd.h"
#include "DX11Simulation/SlotHandle.h"

namespace Canape
{
    class Buffer;
    class ComputeShader;
    class Texture;

    // Each names a resource of the Session that made it, which keeps the resource until it dies.
    using BufferHandle = SlotHandle<Buffer>;
    using TextureHandle = SlotHandle<Texture>;
    using ComputeShaderHandle = SlotHandle<ComputeShader>;
    using VertexShaderHandle = SlotHandle<VertexShaderRef>;
    using PixelShaderHandle = SlotHandle<PixelShaderRef>;
    using SamplerStateHandle = SlotHandle<SamplerStateRef>;
    using BlendStateHandle = SlotHandle<BlendStateRef>;
    using DepthStencilStateHandle = SlotHandle<DepthStencilStateRef>;
    using RasterizerStateHandle = SlotHandle<RasterizerStateRef>;
}
