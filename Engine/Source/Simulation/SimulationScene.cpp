#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "Simulation/SimulationScene.h"

#include "DX11Simulation/Simulator.h"

namespace
{
    // Mirrors cbuffer SimulationOuter in Shader/Simulation.hlsli.
    struct SimulationOuter
    {
        float time = 0.0f;
        float pad_[3];
    };
    static_assert(sizeof(SimulationOuter) == 16);
}

namespace Canape
{
    SimulationScene::SimulationScene(Session& session)
        : session(session)
        , opaquePass(RenderPass::Opaque(session))
        , outer(session, SimulationOuter{}) {}

    void SimulationScene::Advance(float dt)
    {
        time += dt;
    }

    void SimulationScene::Prepare(WaveSurfaces::Components surfaces)
    {
        outer.Upload(
            session,
            SimulationOuter{
                .time = static_cast<float>(time)
            }
        );
        waveSurfaces.Update(session, surfaces);
    }

    void SimulationScene::Render(Simulator& simulator)
    {
        RenderPassScope pass(simulator, opaquePass, outer);
        waveSurfaces.Draw(simulator);
    }
}
