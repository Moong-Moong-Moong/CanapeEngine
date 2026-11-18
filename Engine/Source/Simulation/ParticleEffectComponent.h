#pragma once

#include <string>

#include "Asset/AssetManager.h"
#include "Scene/Component/Component.h"

namespace Canape
{
    // Emits at the Transform position; its rotation and scale are ignored.
    class ParticleEffectComponent final: public Component
    {
        RTTR_ENABLE(Component)

    private:
        std::string effectPath;
        ParticleEffectAssetHandle effect;

    public:
        const ParticleEffectAssetHandle& GetEffect() const
        {
            return effect;
        }

        const std::string& GetEffectPath() const
        {
            return effectPath;
        }
        // An .effect file. An empty path, or a file that does not load, leaves no effect.
        void SetEffectPath(const std::string& path);
    };
}
