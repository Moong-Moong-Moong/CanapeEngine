#pragma once

#include <cstdint>
#include <memory>

#include <d3d11.h>

#include "DX11Simulation/FrameStats.h"
#include "DX11Simulation/Handles.h"

namespace Canape
{
    class DeviceContext;
    class Geometry;
    class GpuTimer;
    class Material;
    class Outer;
    class RenderPass;
    class Session;

    // Runs the simulation on the renderer's D3D11 context
    class Simulator {
    private:
        Session& session;
        std::unique_ptr<DeviceContext> context;

    #if CANAPE_RENDER_DEBUG
        // Only while GPU timing is on, Off by default.
        std::unique_ptr<GpuTimer> timer;
    #endif

        // Of the open frame, or of the next one while none is open; frames count from 0.
        std::uint64_t frameNumber = 0;
        // What the context had counted when the open frame began.
        std::uint64_t dispatchesBefore = 0;
        std::uint64_t drawsBefore = 0;

        CommandCounts counts;
        GpuTiming timing;
    #ifndef NDEBUG
        bool frameOpen = false;
        bool renderOpen = false;
    #endif

    public:
        Simulator(Session&, ID3D11DeviceContext&);
        ~Simulator();

        Simulator(const Simulator&) = delete;
        Simulator& operator=(const Simulator&) = delete;
        Simulator(Simulator&&) = delete;
        Simulator& operator=(Simulator&&) = delete;

        // Dispatches and draws issued between the two are counted and, with GPU timing on, timed.
        void BeginFrame();
        void EndFrame();

        void EnableGpuTiming();
        void DisableGpuTiming();

        // Inside a renderer callback. The pass's render states are not restored.
        void BeginRenderPass(const RenderPass&, const Outer&);
        void Draw(const Material&, const Geometry&);
        void EndRenderPass();

        // Of the frame that just ended.
        const CommandCounts& GetCommandCounts() const
        {
            return counts;
        }

        // Of the last frame whose GPU measurement could be trusted.
        const GpuTiming& GetGpuTiming() const
        {
            return timing;
        }

        DeviceContext& GetDeviceContext() const
        {
            return *context;
        }

    private:
        void bind(std::uint32_t slot, BufferHandle constants);
    };

    class RenderPassScope {
    private:
        Simulator& simulator;

    public:
        RenderPassScope(Simulator& simulator, const RenderPass& pass, const Outer& outer)
            : simulator(simulator)
        {
            simulator.BeginRenderPass(pass, outer);
        }
        ~RenderPassScope()
        {
            simulator.EndRenderPass();
        }

        RenderPassScope(const RenderPassScope&) = delete;
        RenderPassScope& operator=(const RenderPassScope&) = delete;
        RenderPassScope(RenderPassScope&&) = delete;
        RenderPassScope& operator=(RenderPassScope&&) = delete;
    };

    class FrameScope {
    private:
        Simulator& simulator;

    public:
        explicit FrameScope(Simulator& simulator)
            : simulator(simulator)
        {
            simulator.BeginFrame();
        }
        ~FrameScope()
        {
            simulator.EndFrame();
        }

        FrameScope(const FrameScope&) = delete;
        FrameScope& operator=(const FrameScope&) = delete;
        FrameScope(FrameScope&&) = delete;
        FrameScope& operator=(FrameScope&&) = delete;
    };
}
