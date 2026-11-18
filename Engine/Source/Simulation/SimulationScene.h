#pragma once

#include <cstdint>

#include "DX11Simulation/ComputePass.h"
#include "DX11Simulation/Outer.h"
#include "DX11Simulation/RenderPass.h"
#include "Simulation/WaveSurfaces.h"

namespace Canape
{
    class Session;
    class Simulator;

    class SimulationScene
    {
    private:
        Session& session;

        ComputePass computePass;
        RenderPass opaquePass;
        Outer outer;
        WaveSurfaces waveSurfaces;

        // Seconds.
        double time = 0.0;
        std::uint32_t stepNumber = 0;

    public:
        explicit SimulationScene(Session&);
        ~SimulationScene() = default;

        SimulationScene(const SimulationScene&) = delete;
        SimulationScene& operator=(const SimulationScene&) = delete;
        SimulationScene(SimulationScene&&) = delete;
        SimulationScene& operator=(SimulationScene&&) = delete;

        // Advances the clock by dt and steps the GPU state with it.
        void Simulate(Simulator&, WaveSurfaces::Components, float dt);
        // Inside the renderer's after-opaque callback.
        void Render(Simulator&);
    };
}
