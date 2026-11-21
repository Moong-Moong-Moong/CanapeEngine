#include "DX11Simulation/Simulator.h"

#include <cassert>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/ComputePass.h"
#include "DX11Simulation/DeviceContext.h"
#include "DX11Simulation/Field.h"
#include "DX11Simulation/Geometry.h"
#include "DX11Simulation/GpuTimer.h"
#include "DX11Simulation/Kernel.h"
#include "DX11Simulation/Material.h"
#include "DX11Simulation/Outer.h"
#include "DX11Simulation/Particles.h"
#include "DX11Simulation/RenderPass.h"
#include "DX11Simulation/Shader.h"

namespace
{
    template<std::unsigned_integral T>
    constexpr T ceilDiv(T n, T d) noexcept
    {
        return (n + (d - 1)) / d;
    }

    ID3D11Device& deviceOf(ID3D11DeviceContext& context)
    {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        context.GetDevice(&device);
        assert(device != nullptr);

        return *device.Get();
    }
}

namespace Canape
{
    Simulator::~Simulator() = default;

    Simulator::Simulator(ID3D11DeviceContext& context)
        : context(std::make_unique<DeviceContext>(context))
    #if CANAPE_RENDER_DEBUG
        , timer(std::make_unique<GpuTimer>(deviceOf(context), context))
    #endif
    {}

    void Simulator::BeginFrame()
    {
        assert(!frameOpen);

    #ifndef NDEBUG
        frameOpen = true;
    #endif

        dispatchesBefore = context->GetDispatchCount();
        drawsBefore = context->GetDrawCount();

    #if CANAPE_RENDER_DEBUG
        if(auto measured = timer->Collect())
        {
            timing = *measured;
        }
        // Present keeps the GPU within the frames the timer holds. Only frames that run ahead without it,
        // as in a test that never presents, find the timer full, and such a frame goes untimed.
        if(!timer->IsFull())
        [[likely]] {
            timer->BeginFrame(frameNumber);
        }
    #endif
    }

    void Simulator::EndFrame()
    {
        assert(frameOpen);

    #ifndef NDEBUG
        frameOpen = false;
    #endif

        counts.frame = frameNumber++;
        counts.dispatches = static_cast<std::uint32_t>(
            context->GetDispatchCount() - dispatchesBefore
        );
        counts.draws = static_cast<std::uint32_t>(
            context->GetDrawCount() - drawsBefore
        );

    #if CANAPE_RENDER_DEBUG
        if(timer->IsRecording())
        [[likely]] {
            timer->EndFrame();
        }
    #endif
    }

    void Simulator::EnableGpuTiming()
    {
    #if CANAPE_RENDER_DEBUG
        assert(!frameOpen);

        timer->SetEnabled(true);
    #endif
    }

    void Simulator::DisableGpuTiming()
    {
    #if CANAPE_RENDER_DEBUG
        assert(!frameOpen);

        timer->SetEnabled(false);
    #endif
    }

    void Simulator::BeginComputePass(const ComputePass&, const Outer& outer)
    {
        assert(!computeOpen);

    #ifndef NDEBUG
        computeOpen = true;
    #endif

    #if CANAPE_RENDER_DEBUG
        if(timer->IsRecording())
        {
            timer->BeginCompute();
        }
    #endif

        context->SetComputeConstant(
            ComputeOuterSlot,
            outer.GetConstants()
        );
    }

    void Simulator::Dispatch(const Kernel& kernel, Field& field)
    {
        assert(computeOpen);

        const auto& shader = kernel.GetShader();

        const auto& groupSize = shader.GetThreadGroupSize();
        const auto& desc = field.GetDesc();

        context->SetComputeConstant(KernelSlot, kernel.GetConstants());
        context->SetComputeShader(shader);
        context->SetComputeReadable(FieldLatestSlot, field.GetLatest());
        context->SetComputeWritable(FieldNextSlot, field.GetNext());
        context->Dispatch(
            ceilDiv(desc.width, groupSize[0]),
            ceilDiv(desc.height, groupSize[1])
        );
        context->ClearComputeWritable(FieldNextSlot);
        context->ClearComputeReadable(FieldLatestSlot);

        field.Advance();
    }

    void Simulator::Dispatch(const Kernel& kernel, Particles& particles)
    {
        assert(computeOpen);

        const auto& shader = kernel.GetShader();

        context->SetComputeConstant(KernelSlot, kernel.GetConstants());
        context->SetComputeShader(shader);
        if(const auto* map = kernel.GetMap())
        {
            context->SetComputeReadable(ComputeMapSlot, *map);
        }
        context->SetComputeWritable(ComputeParticlesSlot, particles.GetBuffer());
        context->Dispatch(
            ceilDiv(particles.GetDesc().count, shader.GetThreadGroupSize()[0]),
            1
        );
        context->ClearComputeWritable(ComputeParticlesSlot);
        context->ClearComputeReadable(ComputeMapSlot);
    }

    void Simulator::EndComputePass()
    {
        assert(computeOpen);

    #ifndef NDEBUG
        computeOpen = false;
    #endif

        context->ClearComputeShader();
        context->ClearComputeConstant(KernelSlot);
        context->ClearComputeConstant(ComputeOuterSlot);

    #if CANAPE_RENDER_DEBUG
        if(timer->IsRecording())
        {
            timer->EndCompute();
        }
    #endif
    }

    void Simulator::BeginRenderPass(const RenderPass& pass, const Outer& outer)
    {
        assert(!renderOpen);

    #ifndef NDEBUG
        renderOpen = true;
    #endif

    #if CANAPE_RENDER_DEBUG
        if(timer->IsRecording())
        {
            timer->BeginRender();
        }
    #endif

        auto& d3dContext = context->GetRef();
        d3dContext.IASetInputLayout(nullptr);
        d3dContext.RSSetState(pass.GetRasterizer());
        d3dContext.OMSetDepthStencilState(pass.GetDepthStencil(), 0);
        d3dContext.OMSetBlendState(pass.GetBlend(), nullptr, D3D11_DEFAULT_SAMPLE_MASK);
        bind(OuterSlot, outer.GetConstants());
    }

    void Simulator::Draw(const Material& material, const Geometry& geometry)
    {
        assert(renderOpen);

        bind(GeometrySlot, geometry.GetConstants());
        bind(MaterialSlot, material.GetConstants());

        auto& d3dContext = context->GetRef();
        d3dContext.VSSetShader(material.GetVertexShader(), nullptr, 0);
        d3dContext.PSSetShader(material.GetPixelShader(), nullptr, 0);
        context->Draw(geometry.GetVertexCount(), geometry.GetInstanceCount());
    }

    void Simulator::Draw(const Material& material, const Geometry& geometry, const Field& field)
    {
        context->SetVertexReadable(FieldSlot, field.GetLatest());
        Draw(material, geometry);
    }

    void Simulator::Draw(const Material& material, const Particles& particles, std::uint32_t verticesPerParticle)
    {
        assert(renderOpen);
        assert(verticesPerParticle % 3 == 0);

        bind(MaterialSlot, material.GetConstants());
        context->SetVertexReadable(ParticlesSlot, particles.GetBuffer());

        auto& d3dContext = context->GetRef();
        d3dContext.VSSetShader(material.GetVertexShader(), nullptr, 0);
        d3dContext.PSSetShader(material.GetPixelShader(), nullptr, 0);
        context->Draw(verticesPerParticle, particles.GetDesc().count);
    }

    void Simulator::EndRenderPass()
    {
        assert(renderOpen);

    #ifndef NDEBUG
        renderOpen = false;
    #endif

        for(const auto slot : { OuterSlot, GeometrySlot, MaterialSlot })
        {
            context->ClearVertexConstant(slot);
            context->ClearPixelConstant(slot);
        }
        context->ClearVertexReadable(FieldSlot);

    #if CANAPE_RENDER_DEBUG
        if(timer->IsRecording())
        {
            timer->EndRender();
        }
    #endif
    }

    void Simulator::bind(std::uint32_t slot, Buffer& buffer)
    {
        context->SetVertexConstant(slot, buffer);
        context->SetPixelConstant(slot, buffer);
    }
}
