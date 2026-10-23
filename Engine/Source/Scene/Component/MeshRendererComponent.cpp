#include "Scene/Component/MeshRendererComponent.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Core/Application.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderScene.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"
#include "Debug/DebugLog.h"

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

	void MeshRendererComponent::BuildRenderScene(RenderScene& renderScene, const AssetManager& assets, const Renderer& renderer) const
	{
		const MeshAsset* mesh = assets.GetMesh(m_Mesh);
		if (!mesh)
		{
			return;
		}

		const MeshHandle meshHandle = renderer.FindMesh(mesh->GetAssetId());
		if (!meshHandle.IsValid())
		{
			return;
		}

		RenderItem item;
		item.mesh = meshHandle;
		item.world = GetTransform()->GetLocalToWorldMatrix();

		for (uint32_t i = 0; i < mesh->GetSubMeshes().size(); ++i)
		{
			item.submeshIndex = i;
			item.material = {};

			const MaterialAsset* material = assets.GetMaterial(GetMaterial(mesh->GetSubMeshes()[i].materialSlot));
			if (material)
			{
				item.material = renderer.FindMaterial(material->GetAssetId());
			}

			renderScene.items.push_back(item);
		}
	}

	bool MeshRendererComponent::SetMesh(const std::string& path, std::string* outError)
	{
		GameObject* gameObject = GetOwnerObject();
		if (!gameObject)
		{
			CN_LOG_ENGINE("MeshRenderer", "AddComponent 전에 SetMesh 호출됨");
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
		GameObject* gameObject = GetOwnerObject();
		if (!gameObject)
		{
			CN_LOG_ENGINE("MeshRenderer", "AddComponent 전에 SetMaterial 호출됨");
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
