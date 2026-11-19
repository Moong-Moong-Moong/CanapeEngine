#include "Simulation/ParticleEffects.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Asset/AssetManager.h"
#include "Core/Application.h"
#include "DX11Simulation/Simulator.h"
#include "Math/Vector3.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/Scene.h"
#include "Simulation/ParticleEffectComponent.h"

namespace Canape
{
    namespace
    {
        using EmitterVector = std::array<float, 3>;

        // sizeof(Particle) in Shader/ParticleEffect.hlsli.
        constexpr std::uint32_t ParticleStride = 64;

        // What the engine provides, then the effect's parameters. All arguments must outlive the result.
        std::vector<NamedConstant> constantsOf(
            const ParticleEffectAsset& effect,
            const EmitterVector& emitterPosition,
            const EmitterVector& emitterScale)
        {
            std::vector<NamedConstant> constants{
                { "EmitterPosition", emitterPosition },
                { "EmitterScale", emitterScale }
            };
            for(const auto& parameter : effect.GetParameters())
            {
                constants.push_back({ parameter.name, parameter.values });
            }

            return constants;
        }

        std::filesystem::path shaderPathOf(const ParticleEffectAsset& effect)
        {
            const auto& path = effect.GetShaderPath();

            return std::u8string(path.begin(), path.end());
        }

        std::optional<MapTexels> mapOf(const ParticleEffectAsset& effect)
        {
            const auto* map = effect.GetMap();
            if(map == nullptr) return std::nullopt;

            return MapTexels{
                .width = map->GetWidth(),
                .height = map->GetHeight(),
                .texels = std::as_bytes(std::span(map->GetPixels()))
            };
        }
    }

    void ParticleEffects::Update(Session& session, Components components)
    {
        const auto& assets = Application::Get().GetAssetManager();

        EmittersByComponent updated;
        for(const auto* component : components)
        {
            if(!Scene::IsRunnable(component)) continue;

            const auto* effect = assets.GetParticleEffect(component->GetEffect());
            if(effect == nullptr) continue;

            const auto* transform = component->GetTransform();
            const auto position = transform->GetPosition();
            const auto scale = transform->GetLossyScale();
            const EmitterVector emitterPosition{ position.X, position.Y, position.Z };
            const EmitterVector emitterScale{ scale.X, scale.Y, scale.Z };
            const auto constants = constantsOf(*effect, emitterPosition, emitterScale);

            auto kept = emitters.extract(component);
            const bool reusable = !kept.empty() && kept.mapped().effect == effect->GetAssetId();
            if(reusable)
            {
                kept.mapped().kernel.Upload(constants);
                updated.insert(std::move(kept));
                continue;
            }

            const auto shader = shaderPathOf(*effect);
            updated.emplace(
                component,
                Emitter {
                    .effect = effect->GetAssetId(),
                    .pass = effect->GetPass(),
                    .verticesPerParticle = effect->GetVerticesPerParticle(),
                    .particles = Particles(
                        session,
                        { ParticleStride, effect->GetParticleCount() }
                    ),
                    .kernel = Kernel(
                        session,
                        { shader, "cs_main" },
                        constants,
                        mapOf(*effect)
                    ),
                    .material = Material(
                        session,
                        { shader, "vs_main" },
                        { shader, "ps_main" },
                        constants
                    )
                }
            );
        }
        emitters = std::move(updated);
    }

    void ParticleEffects::Step(Simulator& simulator)
    {
        for(auto& emitter : emitters | std::views::values)
        {
            simulator.Dispatch(emitter.kernel, emitter.particles);
        }
    }

    void ParticleEffects::Draw(Simulator& simulator, ParticleEffectPass pass) const
    {
        auto ofPass = emitters | std::views::values | std::views::filter(
            [pass](const auto& emitter)
            {
                return emitter.pass == pass;
            }
        );

        for(const auto& emitter : ofPass)
        {
            simulator.Draw(
                emitter.material,
                emitter.particles,
                emitter.verticesPerParticle
            );
        }
    }
}
