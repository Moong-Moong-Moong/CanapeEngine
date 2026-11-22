#pragma once

#include <span>
#include <unordered_map>

#include "DX11Simulation/Field.h"
#include "DX11Simulation/Geometry.h"
#include "DX11Simulation/Kernel.h"
#include "DX11Simulation/Material.h"

namespace Canape
{
    class Session;
    class Simulator;
    class WaveSurfaceComponent;

    class WaveSurfaces
    {
    public:
        using Components = std::span<WaveSurfaceComponent* const>;

        // The longest step the ripples stay stable at.
        static constexpr float MaxStepSeconds = 1.0f / 60.0f;

    private:
        struct Surface
        {
            Geometry geometry;
            Material material;
            // Height and vertical velocity at each grid point.
            Field ripples;
            Kernel rippleKernel;
        };

        using SurfacesByComponent = std::unordered_map<const WaveSurfaceComponent*, Surface>;

    private:
        // Of the runnable components the last Update was given.
        SurfacesByComponent surfaces;

    public:
        void Update(Session&, Components);
        // Inside the simulator's open compute pass.
        void Step(Simulator&);
        // Inside the simulator's open opaque pass.
        void Draw(Simulator&) const;
    };
}
