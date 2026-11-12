#include "Scene/Component/MeshRendererComponent.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Core/Application.h"
#include "Renderer/Renderer.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"

namespace Canape
{
	namespace
	{
		AssetOwnerInfo MakeOwnerInfo(const GameObject& gameObject)
		{
			AssetOwnerInfo owner;
			owner.sceneName = gameObject.GetScene() ? gameObject.GetScene()->GetName() : std::string();
			owner.objectName = gameObject.GetName();
			owner.componentName = "MeshRendererComponent";
			return owner;
		}
	}

	void MeshRendererComponent::AddRenderItem(const AssetManager& assets, Renderer& renderer) const
	{
		const MeshAsset* mesh = assets.GetMesh(m_Mesh);
		if (!mesh)
		{
			return;
		}

		const Matrix4 world = GetTransform()->GetLocalToWorldMatrix();
		const uint32_t subMeshCount = static_cast<uint32_t>(mesh->GetSubMeshes().size());
		for (uint32_t i = 0; i < subMeshCount; ++i)
		{
			MaterialAssetId materialId;
			const MaterialAsset* material = assets.GetMaterial(GetMaterial(mesh->GetSubMeshes()[i].materialSlot));
			if (material)
			{
				materialId = material->GetAssetId();
			}

			renderer.AddRenderItem(mesh->GetAssetId(), i, materialId, world);
		}
	}

	bool MeshRendererComponent::SetMesh(const std::string& path)
	{
		GameObject* gameObject = GetOwnerObject();
		if (!gameObject)
		{
			m_Mesh.Reset();
			m_DefaultMaterials.clear();
			return false;
		}

		m_MeshPath = AssetManager::ToAssetPath(path);
		m_Mesh = Application::Get().GetAssetManager().GetMeshHandle(m_MeshPath, MakeOwnerInfo(*gameObject));
		// 오버라이드(m_Materials)는 건드리지 않는다. 프로퍼티 적용 순서와 무관하게 같은 결과가 나와야 한다.
		LoadDefaultMaterials();
		return m_Mesh.IsValid();
	}

	void MeshRendererComponent::LoadDefaultMaterials()
	{
		m_DefaultMaterials.clear();

		GameObject* gameObject = GetOwnerObject();
		AssetManager& assets = Application::Get().GetAssetManager();
		const MeshAsset* mesh = assets.GetMesh(m_Mesh);
		if (!gameObject || !mesh)
		{
			return;
		}

		const std::vector<MaterialData>& slots = mesh->GetMaterialSlots();
		m_DefaultMaterials.resize(slots.size());
		for (size_t slot = 0; slot < slots.size(); ++slot)
		{
			// 슬롯 name은 임포트 시 생성된 .mat 경로. 머티리얼 로드 시 텍스처도 함께 로드된다.
			if (MaterialFile::IsMaterialPath(slots[slot].name))
			{
				m_DefaultMaterials[slot] = assets.GetMaterialHandle(slots[slot].name, MakeOwnerInfo(*gameObject));
			}
		}
	}

	bool MeshRendererComponent::SetMaterial(uint32_t slot, const std::string& materialName)
	{
		GameObject* gameObject = GetOwnerObject();
		if (!gameObject)
		{
			return false;
		}

		if (slot >= m_Materials.size()) m_Materials.resize(size_t(slot) + 1);
		if (slot >= m_MaterialNames.size()) m_MaterialNames.resize(size_t(slot) + 1);
		m_MaterialNames[slot] = MaterialFile::IsMaterialPath(materialName) ? AssetManager::ToAssetPath(materialName) : materialName;
		m_Materials[slot] = Application::Get().GetAssetManager().GetMaterialHandle(m_MaterialNames[slot], MakeOwnerInfo(*gameObject));
		return m_Materials[slot].IsValid();
	}

	void MeshRendererComponent::SetMeshPath(const std::string& path)
	{
		if (path.empty())
		{
			m_MeshPath.clear();
			m_Mesh.Reset();
			m_DefaultMaterials.clear();
			return;
		}

		SetMesh(path);
	}

	void MeshRendererComponent::SetMaterialNames(const std::vector<std::string>& names)
	{
		m_Materials.clear();
		m_MaterialNames.clear();
		for (uint32_t slot = 0; slot < static_cast<uint32_t>(names.size()); ++slot)
		{
			if (names[slot].empty())
			{
				m_Materials.resize(size_t(slot) + 1);
				m_MaterialNames.resize(size_t(slot) + 1);
				continue;
			}

			SetMaterial(slot, names[slot]);
		}
	}

	const MaterialAssetHandle& MeshRendererComponent::GetMaterial(uint32_t slot) const
	{
		if (slot < m_Materials.size() && m_Materials[slot].IsValid())
		{
			return m_Materials[slot];
		}
		static const MaterialAssetHandle invalid;
		return slot < m_DefaultMaterials.size() ? m_DefaultMaterials[slot] : invalid;
	}
}
