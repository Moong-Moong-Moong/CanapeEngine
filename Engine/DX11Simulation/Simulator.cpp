#include "DX11Simulation/Simulator.h"

#include <cassert>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/Device.h"
#include "DX11Simulation/DeviceContext.h"
#include "DX11Simulation/Geometry.h"
#include "DX11Simulation/GpuTimer.h"
#include "DX11Simulation/Material.h"
#include "DX11Simulation/Outer.h"
#include "DX11Simulation/RenderPass.h"
#include "DX11Simulation/Session.h"

namespace Canape
{
    Simulator::~Simulator() = default;

    Simulator::Simulator(Session& session, ID3D11DeviceContext& context)
        : session(session),
          context(std::make_unique<DeviceContext>(context)) {}

    void Simulator::BeginFrame()
    {
        assert(!frameOpen);

    #ifndef NDEBUG
        frameOpen = true;
    #endif

        dispatchesBefore = context->GetDispatchCount();
        drawsBefore = context->GetDrawCount();

    #if CANAPE_RENDER_DEBUG
        if(timer != nullptr)
        {
            timer->Collect();
            // Present keeps the GPU within the frames the timer holds. Only frames that run ahead without it,
            // as in a test that never presents, find the timer full, and such a frame goes untimed.
            if(!timer->IsFull())
            [[likely]] {
                timer->BeginFrame(frameNumber);
            }
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
        if(timer != nullptr)
        {
            if(timer->IsRecording())
            [[likely]] {
                timer->EndFrame();
            }

            timing = timer->GetLatest();
        }
    #endif
    }

    void Simulator::EnableGpuTiming()
    {
    #if CANAPE_RENDER_DEBUG
        assert(!frameOpen);

        if(timer != nullptr) return;

        auto& device = session.GetDevice();
        timer = std::make_unique<GpuTimer>(device.CreateGpuTimer());

        timing = {};
    #endif
    }

    void Simulator::DisableGpuTiming()
    {
    #if CANAPE_RENDER_DEBUG
        assert(!frameOpen);

        if(timer == nullptr) return;

        timer = nullptr;

        timing = {};
    #endif
    }

    void Simulator::BeginRenderPass(const RenderPass& pass, const Outer& outer)
    {
        assert(!renderOpen);

    #ifndef NDEBUG
        renderOpen = true;
    #endif

    #if CANAPE_RENDER_DEBUG
        if(timer != nullptr && timer->IsRecording())
        {
            timer->BeginRender();
        }
    #endif

        auto rasterizer = session.Get(pass.GetRasterizer())->Get();
        auto depthStencil = session.Get(pass.GetDepthStencil())->Get();
        auto blendState = session.Get(pass.GetBlend())->Get();
        constexpr UINT sampleMask = D3D11_DEFAULT_SAMPLE_MASK;

        auto& d3dContext = context->GetRef();
        d3dContext.IASetInputLayout(nullptr);
        d3dContext.RSSetState(rasterizer);
        d3dContext.OMSetDepthStencilState(depthStencil, 0);
        d3dContext.OMSetBlendState(blendState, nullptr, sampleMask);
        bind(OuterSlot, outer.GetConstants());
    }

    void Simulator::Draw(const Material& material, const Geometry& geometry)
    {
        assert(renderOpen);

        bind(GeometrySlot, geometry.GetConstants());
        bind(MaterialSlot, material.GetConstants());

        auto vertexShader = session.Get(material.GetVertexShader())->Get();
        auto pixelShader = session.Get(material.GetPixelShader())->Get();

        auto& d3dContext = context->GetRef();
        d3dContext.VSSetShader(vertexShader, nullptr, 0);
        d3dContext.PSSetShader(pixelShader, nullptr, 0);
        context->Draw(geometry.GetVertexCount(), geometry.GetInstanceCount());
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

    #if CANAPE_RENDER_DEBUG
        if(timer != nullptr && timer->IsRecording())
        {
            timer->EndRender();
        }
    #endif
    }

    void Simulator::bind(std::uint32_t slot, BufferHandle constants)
    {
        auto& buffer = *session.Get(constants);
        context->SetVertexConstant(slot, buffer);
        context->SetPixelConstant(slot, buffer);
    }
}
