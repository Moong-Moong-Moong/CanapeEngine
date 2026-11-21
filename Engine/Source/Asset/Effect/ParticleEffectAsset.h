#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Asset/AssetId.h"
#include "Asset/Texture/TextureAsset.h"

namespace Canape
{
    // The render pass an effect's particles are drawn in.
    enum class ParticleEffectPass : std::uint8_t
    {
        // Lit, writing depth, after the opaque meshes.
        Opaque,
        // Added to the scene color after the transparent meshes.
        Additive
    };

    // A value for the shader constant of that name.
    struct ParticleEffectParameter
    {
        std::string name;
        std::vector<float> values;
    };

    class ParticleEffectAsset
    {
    private:
        const ParticleEffectAssetId assetId = allocateAssetId();
        // Relative to the executable.
        std::string shaderPath;
        ParticleEffectPass pass = ParticleEffectPass::Additive;
        // A multiple of three: the vertex shader draws triangles.
        std::uint32_t verticesPerParticle = 0;
        std::uint32_t particleCount = 0;
        std::vector<ParticleEffectParameter> parameters;

        std::unique_ptr<TextureAsset> map;

    public:
        ParticleEffectAsset(
            std::string shaderPath,
            ParticleEffectPass pass,
            std::uint32_t verticesPerParticle,
            std::uint32_t particleCount,
            std::vector<ParticleEffectParameter> parameters,
            std::unique_ptr<TextureAsset> map
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
        ParticleEffectPass GetPass() const
        {
            return pass;
        }
        std::uint32_t GetVerticesPerParticle() const
        {
            return verticesPerParticle;
        }
        std::uint32_t GetParticleCount() const
        {
            return particleCount;
        }
        const std::vector<ParticleEffectParameter>& GetParameters() const
        {
            return parameters;
        }
        const TextureAsset* GetMap() const
        {
            return map.get();
        }

    private:
        static ParticleEffectAssetId allocateAssetId();
    };
}
