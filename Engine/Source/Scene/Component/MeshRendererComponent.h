#pragma once

#include "Scene/Component/Component.h"
#include "Renderer/Mesh.h"
#include "Renderer/MaterialAsset.h"

#include <memory>
#include <utility>
#include <vector>

namespace Canape
{
	class MeshRendererComponent : public Component
	{
	public:
		MeshRendererComponent() = default;
		explicit MeshRendererComponent(std::shared_ptr<Mesh> mesh) : m_Mesh(std::move(mesh)) {}

		const std::shared_ptr<Mesh>& GetMesh() const { return m_Mesh; }
		void SetMesh(std::shared_ptr<Mesh> mesh) { m_Mesh = std::move(mesh); }

		void SetMaterial(uint32_t slot, std::shared_ptr<MaterialAsset> material)
		{
			if (slot >= m_Materials.size()) m_Materials.resize(size_t(slot) + 1);
			m_Materials[slot] = std::move(material);
		}
		std::shared_ptr<MaterialAsset> GetMaterial(uint32_t slot) const
		{
			return slot < m_Materials.size() ? m_Materials[slot] : nullptr;
		}

	private:
		std::shared_ptr<Mesh> m_Mesh;
		std::vector<std::shared_ptr<MaterialAsset>> m_Materials;
	};
}
