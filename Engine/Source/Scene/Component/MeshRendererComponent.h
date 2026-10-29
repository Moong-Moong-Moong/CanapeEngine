#pragma once

#include "Scene/Component/RenderableComponent.h"
#include "Asset/AssetManager.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Canape
{
	class MeshRendererComponent : public RenderableComponent
	{
	public:
		MeshRendererComponent() = default;
		void AddRenderItem(const AssetManager& assets, Renderer& renderer) const override;
		bool SetMesh(const std::string& path);
		const MeshAssetHandle& GetMesh() const { return m_Mesh; }

		bool SetMaterial(uint32_t slot, const std::string& materialName);

		const MaterialAssetHandle& GetMaterial(uint32_t slot) const;

	private:
		MeshAssetHandle m_Mesh;
		std::vector<MaterialAssetHandle> m_Materials;
	};
}
