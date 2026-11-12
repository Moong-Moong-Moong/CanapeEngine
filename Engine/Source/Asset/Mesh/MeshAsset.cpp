#include "Asset/Mesh/MeshAsset.h"

#include <stdexcept>
#include <utility>

namespace Canape
{
	MeshAsset::MeshAsset(std::vector<Vector3> positions, std::vector<uint32_t> indices)
		: m_Indices(std::move(indices))
	{
		m_Vertices.reserve(positions.size());
		for (const Vector3& position : positions)
			m_Vertices.push_back({ position, {}, {} });
		InitializeSubMeshes();
	}

	MeshAsset::MeshAsset(std::vector<StaticVertex> vertices, std::vector<uint32_t> indices, std::vector<SubMesh> submeshes, std::vector<MaterialData> materialSlots)
		: m_Vertices(std::move(vertices)), m_Indices(std::move(indices)), m_SubMeshes(std::move(submeshes)), m_materialSlots(std::move(materialSlots))
	{
		InitializeSubMeshes();
	}

	void MeshAsset::InitializeSubMeshes()
	{
		if (const char* error = ValidateMeshIndices(m_Vertices.size(), m_Indices, m_SubMeshes))
			throw std::invalid_argument(error);
		if (m_SubMeshes.empty())
			m_SubMeshes.push_back({ 0, static_cast<uint32_t>(m_Indices.size()), 0, 0 });
	}

	MeshAssetId MeshAsset::AllocateAssetId()
	{
		// Current asset creation is single-threaded. Clear/reload never resets this counter.
		static uint64_t nextId = 1;
		if (nextId == 0)
		{
			throw std::overflow_error("Mesh asset ID space exhausted");
		}
		return MeshAssetId{ nextId++ };
	}
}
