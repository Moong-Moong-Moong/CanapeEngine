#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "Simulation/SimulationScene.h"

#include <algorithm>
#include <cmath>

#include "DX11Simulation/Simulator.h"

namespace
{
    constexpr std::uint32_t MaxStepsPerFrame = 4;

    // Mirrors cbuffer SimulationOuter in Shader/Simulation.hlsli.
    struct SimulationOuter
    {
        float time = 0.0f;
        // Of the step being computed.
        float stepDeltaTime = 0.0f;
        std::uint32_t stepNumber = 0;
        float pad_ = 0.0f;
    };
    static_assert(sizeof(SimulationOuter) == 16);
}

namespace Canape
{
    SimulationScene::SimulationScene(Session& session)
        : session(session)
        , opaquePass(RenderPass::Opaque(session))
        , outer(session, SimulationOuter{}) {}

    void SimulationScene::Simulate(Simulator& simulator, WaveSurfaces::Components surfaces, float dt)
    {
        // A longer frame is not caught up with.
        constexpr float MaxSteppedTime = MaxStepsPerFrame * WaveSurfaces::MaxStepSeconds;

        time += dt;
        SimulationOuter values{
            .time = static_cast<float>(time)
        };
        // The draws read what the last step left, or this when there is no step.
        outer.Upload(session, values);
        waveSurfaces.Update(session, surfaces);

        const auto steppedTime = std::clamp(dt, 0.0f, MaxSteppedTime);
        // Equal steps, each as long as the ripples allow at most.
        const auto steps = static_cast<std::uint32_t>(
            std::ceil(steppedTime / WaveSurfaces::MaxStepSeconds)
        );
        for(std::uint32_t i = 0; i < steps; ++i)
        {
            values.stepDeltaTime = steppedTime / static_cast<float>(steps);
            values.stepNumber = stepNumber++;
            outer.Upload(session, values);

            ComputePassScope pass(simulator, computePass, outer);
            waveSurfaces.Step(simulator);
        }
    }

    void SimulationScene::Render(Simulator& simulator)
    {
        RenderPassScope pass(simulator, opaquePass, outer);
        waveSurfaces.Draw(simulator);
    }
}
