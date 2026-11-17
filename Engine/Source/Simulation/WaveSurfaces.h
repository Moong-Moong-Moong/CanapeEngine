#pragma once

#include <span>
#include <unordered_map>

#include "DX11Simulation/Geometry.h"
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

    private:
        struct Surface
        {
            Geometry geometry;
            Material material;
        };

        using SurfacesByComponent = std::unordered_map<const WaveSurfaceComponent*, Surface>;

    private:
        // Of the runnable components the last Update was given.
        SurfacesByComponent surfaces;

    public:
        void Update(Session&, Components);
        // Inside the simulator's open opaque pass.
        void Draw(Simulator&) const;
    };
}
