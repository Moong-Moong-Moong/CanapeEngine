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

    // Mirrors Shader/Swell.hlsli and the constants it takes from Shader/Simulation.hlsli.
    struct SwellWave
    {
        float wavelengthScale = 0.0f;
        float amplitudeScale = 0.0f;
        // Radians.
        float turn = 0.0f;
    };

    constexpr float TwoPi = 6.28318530718f;
    constexpr float Gravity = 9.81f;
    constexpr float SwellAmplitudeFactor = 0.105f;
    constexpr std::array<SwellWave, 3> SwellWaves {{
        { 1.0f, 1.0f, 0.0f },
        { 0.53f, 0.45f, 0.6f },
        { 0.29f, 0.2f, -0.9f }
    }};
    // What rides the swell ignores shorter waves, as in Shader/Leaves.hlsl and Shader/Rain.hlsl.
    constexpr float ShortestRiddenWavelength = 1.0f;

    float swellHeight(const Canape::Vector2& xz, const Canape::Vector2& windVelocity, float time)
    {
        const auto windSpeed = windVelocity.Length();
        if(windSpeed == 0.0f) return 0.0f;

        const auto windDirection = windVelocity / windSpeed;
        const auto mainWavelength = TwoPi * windSpeed * windSpeed / Gravity;
        const auto mainAmplitude = SwellAmplitudeFactor * windSpeed * windSpeed / Gravity;

        float height = 0.0f;
        for(const auto& wave : SwellWaves)
        {
            const auto sinTurn = std::sin(wave.turn);
            const auto cosTurn = std::cos(wave.turn);
            const Canape::Vector2 direction {
                windDirection.X * cosTurn - windDirection.Y * sinTurn,
                windDirection.X * sinTurn + windDirection.Y * cosTurn
            };
            const auto wavelength = mainWavelength * wave.wavelengthScale;
            const auto k = TwoPi / std::max(wavelength, ShortestRiddenWavelength);

            const auto phase = k * (
                xz.X * direction.X + xz.Y * direction.Y
                - windSpeed * std::sqrt(wave.wavelengthScale) * time
            );
            const auto amplitude = mainAmplitude * wave.amplitudeScale
                * std::clamp(wavelength / ShortestRiddenWavelength - 1.0f, 0.0f, 1.0f);
            height += amplitude * std::sin(phase);
        }
        return height;
    }
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

    float SimulationScene::SwellHeightAt(const Vector2& xz) const
    {
        return swellHeight(xz, windVelocity, static_cast<float>(time));
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
        windVelocity = findWindVelocity(registry);
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

    void SimulationScene::RenderMirror(Simulator& simulator)
    {
        {
            RenderPassScope pass(simulator, opaquePass, outer);
            terrainGeneration.Draw(simulator);
            particleEffects.Draw(simulator, ParticleEffectPass::Opaque);
        }

        {
            RenderPassScope pass(simulator, additivePass, outer);
            particleEffects.Draw(simulator, ParticleEffectPass::Additive);
        }
    }
}
