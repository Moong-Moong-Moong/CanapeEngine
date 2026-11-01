#include "Scene/Component/SkyboxComponent.h"
#include "Core/Application.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"

namespace Canape
{
	bool SkyboxComponent::SetCubemap(const std::string& path)
	{
		GameObject* gameObject = GetOwnerObject();
		if (!gameObject)
		{
			m_Cubemap.Reset();
			return false;
		}

		AssetOwnerInfo owner;
		owner.sceneName = gameObject->GetScene() ? gameObject->GetScene()->GetName() : std::string();
		owner.objectName = gameObject->GetName();
		owner.componentName = "SkyboxComponent";
		m_Cubemap = Application::Get().GetAssetManager().GetCubemapHandle(path, owner);
		return m_Cubemap.IsValid();
	}

	RenderSkybox SkyboxComponent::BuildRenderSkybox() const
	{
		RenderSkybox skybox;
		skybox.brightness = m_Brightness;
		skybox.rotationRadians = m_RotationRadians;
		return skybox;
	}
}
