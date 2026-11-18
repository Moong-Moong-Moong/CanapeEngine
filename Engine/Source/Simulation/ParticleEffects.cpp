#include "Simulation/ParticleEffects.h"

#include <array>
#include <cstdint>
#include <filesystem>
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
        using EmitterPosition = std::array<float, 3>;

        // sizeof(Particle) in Shader/ParticleEffect.hlsli.
        constexpr std::uint32_t ParticleStride = 48;

        // What the engine provides, then the effect's parameters. Both arguments must outlive the result.
        std::vector<NamedConstant> constantsOf(
            const ParticleEffectAsset& effect,
            const EmitterPosition& emitterPosition)
        {
            std::vector<NamedConstant> constants{
                { "EmitterPosition", emitterPosition }
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

            const auto position = component->GetTransform()->GetPosition();
            const EmitterPosition emitterPosition{ position.X, position.Y, position.Z };
            const auto constants = constantsOf(*effect, emitterPosition);

            auto kept = emitters.extract(component);
            const bool reusable = !kept.empty() && kept.mapped().effect == effect->GetAssetId();
            if(!reusable)
            {
                const auto shader = shaderPathOf(*effect);
                updated.emplace(
                    component,
                    Emitter {
                        .effect = effect->GetAssetId(),
                        .particles = Particles(
                            session,
                            { ParticleStride, effect->GetParticleCount() }
                        ),
                        .kernel = Kernel(
                            session,
                            { shader, "cs_main" },
                            constants
                        ),
                        .material = Material(
                            session,
                            { shader, "vs_main" },
                            { shader, "ps_main" },
                            constants
                        )
                    }
                );
                continue;
            }

            kept.mapped().kernel.Upload(constants);
            updated.insert(std::move(kept));
        }
        emitters = std::move(updated);
    }

    void ParticleEffects::Step(Simulator& simulator)
    {
        for(auto& [component, emitter] : emitters)
        {
            simulator.Dispatch(emitter.kernel, emitter.particles);
        }
    }

    void ParticleEffects::Draw(Simulator& simulator) const
    {
        for(const auto& [component, emitter] : emitters)
        {
            simulator.Draw(emitter.material, emitter.particles);
        }
    }
}
