#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Asset/AssetId.h"

namespace Canape
{
    // A value for the shader constant of that name.
    struct ParticleEffectParameter
    {
        std::string name;
        std::vector<float> values;
    };

    // What an .effect file says: which shader steps and draws how many particles, with which values.
    class ParticleEffectAsset
    {
    private:
        const ParticleEffectAssetId assetId = allocateAssetId();
        // Relative to the executable.
        std::string shaderPath;
        std::uint32_t particleCount = 0;
        std::vector<ParticleEffectParameter> parameters;

    public:
        ParticleEffectAsset(
            std::string shaderPath,
            std::uint32_t particleCount,
            std::vector<ParticleEffectParameter> parameters
        );
        ~ParticleEffectAsset() = default;

        ParticleEffectAsset(const ParticleEffectAsset&) = delete;
        ParticleEffectAsset& operator=(const ParticleEffectAsset&) = delete;
        ParticleEffectAsset(ParticleEffectAsset&&) = delete;
        ParticleEffectAsset& operator=(ParticleEffectAsset&&) = delete;

        // nullptr, with the reason logged, when the file is not a particle effect.
        static std::unique_ptr<ParticleEffectAsset> Load(const std::string& path);

        ParticleEffectAssetId GetAssetId() const
        {
            return assetId;
        }
        const std::string& GetShaderPath() const
        {
            return shaderPath;
        }
        std::uint32_t GetParticleCount() const
        {
            return particleCount;
        }
        const std::vector<ParticleEffectParameter>& GetParameters() const
        {
            return parameters;
        }

    private:
        static ParticleEffectAssetId allocateAssetId();
    };
}
