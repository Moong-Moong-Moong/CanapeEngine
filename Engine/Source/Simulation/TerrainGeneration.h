#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "DX11Simulation/Field.h"
#include "DX11Simulation/Geometry.h"
#include "DX11Simulation/Kernel.h"
#include "DX11Simulation/Material.h"

namespace Canape
{
    class Session;
    class Simulator;
    class TerrainGeneratorComponent;

    class TerrainGeneration
    {
    public:
        using Components = std::span<TerrainGeneratorComponent* const>;

    private:
        struct Inputs
        {
            std::uint32_t seed = 0;
            float amount = 0.0f;
        };

        struct Workspace
        {
            Geometry geometry;
            Material material;
            // Bedrock height and cover thickness in meters at each grid point.
            Field ground;
            Kernel upliftKernel;
            std::vector<const Kernel*> remainingStages;
        };

        using InputsByComponent = std::unordered_map<const TerrainGeneratorComponent*, Inputs>;
        using WorkspacesByComponent = std::unordered_map<const TerrainGeneratorComponent*, Workspace>;

    private:
        InputsByComponent requests;
        WorkspacesByComponent workspaces;

    public:
        void Request(const TerrainGeneratorComponent&);
        void Update(Session&, Components);
        // Inside the simulator's open compute pass.
        void Step(Simulator&);
        // Inside the simulator's open opaque pass.
        void Draw(Simulator&) const;
    };
}
