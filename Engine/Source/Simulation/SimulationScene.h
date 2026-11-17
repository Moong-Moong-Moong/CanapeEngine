#pragma once

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

        RenderPass opaquePass;
        Outer outer;
        WaveSurfaces waveSurfaces;

        // Seconds.
        double time = 0.0;

    public:
        explicit SimulationScene(Session&);
        ~SimulationScene() = default;

        SimulationScene(const SimulationScene&) = delete;
        SimulationScene& operator=(const SimulationScene&) = delete;
        SimulationScene(SimulationScene&&) = delete;
        SimulationScene& operator=(SimulationScene&&) = delete;

        void Advance(float dt);
        void Prepare(WaveSurfaces::Components surfaces);
        // Inside the renderer's after-opaque callback of the frame Prepare was called for.
        void Render(Simulator&);
    };
}
