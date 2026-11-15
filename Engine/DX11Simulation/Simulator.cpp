#include "DX11Simulation/Simulator.h"

#include <cassert>

#include "DX11Simulation/Device.h"
#include "DX11Simulation/DeviceContext.h"
#include "DX11Simulation/GpuTimer.h"
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
}
