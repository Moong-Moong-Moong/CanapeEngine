#include "Simulation/ParticleEffectComponent.h"

#include "Core/Application.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"

namespace Canape
{
    void ParticleEffectComponent::SetEffectPath(const std::string& path)
    {
        auto& assets = Application::Get().GetAssetManager();

        effectPath = AssetManager::ToAssetPath(path);
        effect.Reset();

        const auto* gameObject = GetOwnerObject();
        if(effectPath.empty() || gameObject == nullptr) return;

        const auto* scene = gameObject->GetScene();
        effect = assets.GetParticleEffectHandle(
            effectPath,
            {
                .sceneName = scene != nullptr ?
                    scene->GetName() :
                    std::string(),
                .objectName = gameObject->GetName(),
                .componentName = "ParticleEffectComponent"
            }
        );
    }
}
