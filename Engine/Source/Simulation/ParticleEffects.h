#pragma once

#include <span>
#include <unordered_map>

#include "Asset/AssetId.h"
#include "DX11Simulation/Kernel.h"
#include "DX11Simulation/Material.h"
#include "DX11Simulation/Particles.h"

namespace Canape
{
    class ParticleEffectComponent;
    class Session;
    class Simulator;

    class ParticleEffects
    {
    public:
        using Components = std::span<ParticleEffectComponent* const>;

    private:
        struct Emitter
        {
            // What the rest was made from.
            ParticleEffectAssetId effect;
            Particles particles;
            Kernel kernel;
            Material material;
        };

        using EmittersByComponent = std::unordered_map<const ParticleEffectComponent*, Emitter>;

    private:
        // Of the runnable components the last Update was given that have an effect.
        EmittersByComponent emitters;

    public:
        // Throws as Kernel and Material do when an effect's shader and its parameters disagree.
        void Update(Session&, Components);
        // Inside the simulator's open compute pass.
        void Step(Simulator&);
        // Inside the simulator's open additive pass.
        void Draw(Simulator&) const;
    };
}
