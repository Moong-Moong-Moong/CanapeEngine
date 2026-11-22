#pragma once

#include <array>
#include <cstdint>

#include "DX11Simulation/ComputePass.h"
#include "DX11Simulation/Outer.h"
#include "DX11Simulation/RenderPass.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Simulation/ParticleEffects.h"
#include "Simulation/TerrainGeneration.h"
#include "Simulation/WaveSurfaces.h"

namespace Canape
{
    class ComponentRegistry;
    class Session;
    class Simulator;
    class TerrainGeneratorComponent;

    inline constexpr std::uint32_t MaxWaveDisturbances = 8;

    // A push on the water around a point.
    struct WaveDisturbance
    {
        // World XZ.
        Vector2 center;
        float radius = 0.0f;
        // Meters per second, up when positive.
        float strength = 0.0f;
    };

    class SimulationScene
    {
    private:
        Session& session;

        ComputePass computePass;
        RenderPass opaquePass;
        RenderPass additivePass;
        Outer outer;
        WaveSurfaces waveSurfaces;
        ParticleEffects particleEffects;
        TerrainGeneration terrainGeneration;

        // Seconds.
        double time = 0.0;
        std::uint32_t stepNumber = 0;
        // Waiting for the next step.
        std::array<WaveDisturbance, MaxWaveDisturbances> disturbances;
        std::uint32_t disturbanceCount = 0;

    public:
        explicit SimulationScene(Session&);
        ~SimulationScene() = default;

        SimulationScene(const SimulationScene&) = delete;
        SimulationScene& operator=(const SimulationScene&) = delete;
        SimulationScene(SimulationScene&&) = delete;
        SimulationScene& operator=(SimulationScene&&) = delete;

        // Pushes the water around position once, in the next step.
        // Ignored once MaxWaveDisturbances are waiting.
        void Disturb(const Vector3& position, float radius, float strength);
        void GenerateTerrain(const TerrainGeneratorComponent&);
        void Simulate(Simulator&, const ComponentRegistry&, float dt);

        void RenderAfterOpaque(Simulator&);
        void RenderAfterTransparent(Simulator&);
        void RenderMirror(Simulator&);
    };
}
