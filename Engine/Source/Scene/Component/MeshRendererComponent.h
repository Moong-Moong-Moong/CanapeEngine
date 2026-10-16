#pragma once

#include "Scene/Component/Component.h"
#include "Asset/AssetManager.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Canape
{
	class MeshRendererComponent : public Component
	{
	public:
		MeshRendererComponent() = default;

		// Call after AddComponent: the owning scene/object names are recorded at acquisition.
		// Replaces the current mesh. On failure the component keeps no mesh and returns false.
		bool SetMesh(const std::string& path, std::string* outError = nullptr);
		// Resolve through AssetManager::GetMesh.
		const MeshAssetHandle& GetMesh() const { return m_Mesh; }

		// Same rules as SetMesh. The material must already exist (AssetManager::CreateMaterial).
		bool SetMaterial(uint32_t slot, const std::string& materialName, std::string* outError = nullptr);
		// Resolve through AssetManager::GetMaterial. Invalid for empty or out-of-range slots.
		const MaterialAssetHandle& GetMaterial(uint32_t slot) const;

	private:
		MeshAssetHandle m_Mesh;
		std::vector<MaterialAssetHandle> m_Materials;
	};
}
