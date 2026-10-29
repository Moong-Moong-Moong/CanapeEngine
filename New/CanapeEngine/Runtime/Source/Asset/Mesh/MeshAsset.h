#pragma once

#include "Asset/AssetId.h"
#include "Asset/Mesh/MeshData.h"
#include "Math/Vector3.h"

#include <cstdint>
#include <vector>

namespace Canape
{
	class MeshAsset
	{
	public:
		MeshAsset() = default;
		MeshAsset(const MeshAsset&) = delete;
		MeshAsset& operator=(const MeshAsset&) = delete;
		MeshAsset(std::vector<StaticVertex> vertices, std::vector<uint32_t> indices, std::vector<DX11SubMesh> submeshes);

		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
		const std::vector<StaticVertex>& GetVertices() const { return m_Vertices; }
		const std::vector<DX11SubMesh>& GetSubMeshes() const { return m_SubMeshes; }

		VertexFormat GetVertexFormat() const { return VertexFormat::Static; }

		uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_Vertices.size()); }
		uint32_t GetIndexCount() const { return static_cast<uint32_t>(m_Indices.size()); }

		MeshAssetId GetAssetId() const { return m_AssetId; }

	private:
		void InitializeSubMeshes();
		static MeshAssetId AllocateAssetId();

		const MeshAssetId m_AssetId = AllocateAssetId();
		std::vector<StaticVertex> m_Vertices;
		std::vector<uint32_t> m_Indices;
		std::vector<DX11SubMesh> m_SubMeshes;
	};
}
