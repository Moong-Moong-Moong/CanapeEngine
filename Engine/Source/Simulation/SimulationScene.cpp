#include "Simulation/SimulationScene.h"

#include <algorithm>
#include <cmath>

#include "DX11Simulation/Simulator.h"
#include "Scene/Component/ComponentRegistry.h"
#include "Scene/Scene.h"
#include "Simulation/WindComponent.h"

namespace
{
    constexpr std::uint32_t MaxStepsPerFrame = 4;
    // The kernel divides by the radius.
    constexpr float MinDisturbanceRadius = 0.01f;

    // Mirrors cbuffer SimulationOuter in Shader/Simulation.hlsli.
    struct SimulationOuter
    {
        float time = 0.0f;
        // Of the step being computed.
        float stepDeltaTime = 0.0f;
        std::uint32_t stepNumber = 0;
        std::uint32_t disturbanceCount = 0;
        // Unit in world XZ; zero when calm.
        Canape::Vector2 windDirection;
        float windSpeed = 0.0f;
        float pad_;
        std::array<Canape::WaveDisturbance, Canape::MaxWaveDisturbances> disturbances;
    };
    static_assert(sizeof(SimulationOuter) == 160);
}

namespace Canape
{
    namespace
    {
        Vector2 findWindVelocity(const ComponentRegistry& registry)
        {
            for(auto* wind : registry.GetWinds())
            {
                if(Scene::IsRunnable(wind))
                {
                    return wind->GetVelocity();
                }
            }
            return Vector2::Zero;
        }
    }

    SimulationScene::SimulationScene(Session& session)
        : session(session)
        , opaquePass(RenderPass::Opaque(session))
        , additivePass(RenderPass::Additive(session))
        , outer(session, SimulationOuter{}) {}

    void SimulationScene::Disturb(const Vector3& position, float radius, float strength)
    {
        if(disturbanceCount == MaxWaveDisturbances) return;

        disturbances[disturbanceCount++] = {
            .center = { position.X, position.Z },
            .radius = std::max(radius, MinDisturbanceRadius),
            .strength = strength
        };
    }

    void SimulationScene::GenerateTerrain(const TerrainGeneratorComponent& component)
    {
        terrainGeneration.Request(component);
    }

    void SimulationScene::Simulate(
        Simulator& simulator,
        const ComponentRegistry& registry,
        float dt)
    {
        // A longer frame is not caught up with.
        constexpr float MaxSteppedTime = MaxStepsPerFrame * WaveSurfaces::MaxStepSeconds;

        time += dt;
        const auto windVelocity = findWindVelocity(registry);
        const auto windSpeed = windVelocity.Length();
        SimulationOuter values{
            .time = static_cast<float>(time),
            .windDirection = windSpeed > 0.0f ?
                windVelocity / windSpeed :
                Vector2{},
            .windSpeed = windSpeed
        };
        // The draws read what the last step left, or this when there is no step.
        outer.Upload(values);
        waveSurfaces.Update(session, registry.GetWaveSurfaces());
        particleEffects.Update(session, registry.GetParticleEffects());
        terrainGeneration.Update(session, registry.GetTerrainGenerators());

        {
            ComputePassScope pass(simulator, computePass, outer);
            terrainGeneration.Step(simulator);
        }

        const auto steppedTime = std::clamp(dt, 0.0f, MaxSteppedTime);
        // Equal steps, each as long as the ripples allow at most.
        const auto steps = static_cast<std::uint32_t>(
            std::ceil(steppedTime / WaveSurfaces::MaxStepSeconds)
        );
        for(std::uint32_t i = 0; i < steps; ++i)
        {
            values.stepDeltaTime = steppedTime / static_cast<float>(steps);
            values.stepNumber = stepNumber++;
            values.disturbanceCount = disturbanceCount;
            values.disturbances = disturbances;
            outer.Upload(values);
            // A disturbance pushes once, so only the first step carries it.
            disturbanceCount = 0;

            ComputePassScope pass(simulator, computePass, outer);
            waveSurfaces.Step(simulator);
            particleEffects.Step(simulator);
        }
    }

    void SimulationScene::RenderAfterOpaque(Simulator& simulator)
    {
        RenderPassScope pass(simulator, opaquePass, outer);
        waveSurfaces.Draw(simulator);
        terrainGeneration.Draw(simulator);
        particleEffects.Draw(simulator, ParticleEffectPass::Opaque);
    }

    void SimulationScene::RenderAfterTransparent(Simulator& simulator)
    {
        RenderPassScope pass(simulator, additivePass, outer);
        particleEffects.Draw(simulator, ParticleEffectPass::Additive);
    }
}
