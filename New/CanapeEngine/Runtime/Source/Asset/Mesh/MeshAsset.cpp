#include "Asset/Mesh/MeshAsset.h"

#include <stdexcept>
#include <utility>

namespace Canape
{
	MeshAsset::MeshAsset(std::vector<StaticVertex> vertices, std::vector<uint32_t> indices, std::vector<DX11SubMesh> submeshes)
		: m_Vertices(std::move(vertices)), m_Indices(std::move(indices)), m_SubMeshes(std::move(submeshes))
	{
		InitializeSubMeshes();
	}

	void MeshAsset::InitializeSubMeshes()
	{
		// 메쉬 유효성 검사
		if (!ValidateMeshIndices(m_Vertices.size(), m_Indices, m_SubMeshes))
			return;

		// 서브 메쉬가 없다면 기본값을 넣음
		if (m_SubMeshes.empty())
			m_SubMeshes.push_back({ 0, static_cast<uint32_t>(m_Indices.size()), 0, 0 });
	}

	MeshAssetId MeshAsset::AllocateAssetId()
	{
		static uint64_t nextId = 1;
		if (nextId == 0)
		{
			throw std::overflow_error("Mesh asset ID space exhausted");
		}
		return MeshAssetId{ nextId++ };
	}
}
