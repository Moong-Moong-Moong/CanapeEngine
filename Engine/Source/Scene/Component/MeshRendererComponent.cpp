#include "Scene/Component/MeshRendererComponent.h"
#include "Core/Application.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"

namespace Canape
{
	namespace
	{
		constexpr char kAttachError[] = "MeshRendererComponent must be added with AddComponent first";

		AssetOwnerInfo MakeOwnerInfo(const GameObject& gameObject)
		{
			AssetOwnerInfo owner;
			owner.sceneName = gameObject.GetScene() ? gameObject.GetScene()->GetName() : std::string();
			owner.objectName = gameObject.GetName();
			owner.componentName = "MeshRendererComponent";
			return owner;
		}
	}

	bool MeshRendererComponent::SetMesh(const std::string& path, std::string* outError)
	{
		GameObject* gameObject = GetGameObject();
		if (!gameObject)
		{
			if (outError) *outError = kAttachError;
			m_Mesh.Reset();
			return false;
		}

		// Acquire before the old handle is released so re-setting the same path never unloads it.
		m_Mesh = Application::Get().GetAssetManager().GetMeshHandle(path, MakeOwnerInfo(*gameObject), outError);
		return m_Mesh.IsValid();
	}

	bool MeshRendererComponent::SetMaterial(uint32_t slot, const std::string& materialName, std::string* outError)
	{
		GameObject* gameObject = GetGameObject();
		if (!gameObject)
		{
			if (outError) *outError = kAttachError;
			return false;
		}

		if (slot >= m_Materials.size()) m_Materials.resize(size_t(slot) + 1);
		m_Materials[slot] = Application::Get().GetAssetManager().GetMaterialHandle(materialName, MakeOwnerInfo(*gameObject), outError);
		return m_Materials[slot].IsValid();
	}

	const MaterialAssetHandle& MeshRendererComponent::GetMaterial(uint32_t slot) const
	{
		static const MaterialAssetHandle invalid;
		return slot < m_Materials.size() ? m_Materials[slot] : invalid;
	}
}
