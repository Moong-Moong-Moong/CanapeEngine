#pragma once

#include "MeshAssetId.h"
#include "MeshData.h"
#include "Math/Vector3.h"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Canape
{
	class Mesh
	{
	public:
		Mesh() = default;
		Mesh(const Mesh&) = delete;
		Mesh& operator=(const Mesh&) = delete;
		Mesh(std::vector<Vector3> positions, std::vector<uint32_t> indices)
			: m_Positions(std::move(positions))
			, m_Indices(std::move(indices))
		{
			m_Vertices.reserve(m_Positions.size());
			for (const Vector3& position : m_Positions)
				m_Vertices.push_back({ position, {}, {} });
			InitializeSubMeshes();
		}

		Mesh(std::vector<StaticVertex> vertices, std::vector<uint32_t> indices, std::vector<SubMesh> submeshes)
			: m_Vertices(std::move(vertices)), m_Indices(std::move(indices)), m_SubMeshes(std::move(submeshes))
		{
			// Retain the existing position-only accessor for callers of the old Mesh API.
			m_Positions.reserve(m_Vertices.size());
			for (const StaticVertex& vertex : m_Vertices) m_Positions.push_back(vertex.Position);
			InitializeSubMeshes();
		}

		const std::vector<Vector3>& GetPositions() const { return m_Positions; }
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
		const std::vector<StaticVertex>& GetVertices() const { return m_Vertices; }
		const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }
		// CPU data is canonical StaticVertex, including meshes built by the old position-only constructor.
		VertexFormat GetVertexFormat() const { return VertexFormat::Static; }

		uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_Vertices.size()); }
		uint32_t GetIndexCount() const { return static_cast<uint32_t>(m_Indices.size()); }

		MeshAssetId GetAssetId() const { return m_AssetId; }

	private:
		void InitializeSubMeshes()
		{
			if (const char* error = ValidateMeshIndices(m_Vertices.size(), m_Indices, m_SubMeshes))
				throw std::invalid_argument(error);
			if (m_SubMeshes.empty())
				m_SubMeshes.push_back({ 0, static_cast<uint32_t>(m_Indices.size()), 0, 0 });
		}

		static MeshAssetId AllocateAssetId()
		{
			// Current asset creation is single-threaded. Clear/reload never resets this counter.
			static uint64_t nextId = 1;
			if (nextId == 0)
			{
				throw std::overflow_error("Mesh asset ID space exhausted");
			}
			return MeshAssetId{ nextId++ };
		}

		const MeshAssetId m_AssetId = AllocateAssetId();
		std::vector<Vector3> m_Positions;
		std::vector<StaticVertex> m_Vertices;
		std::vector<uint32_t> m_Indices;
		std::vector<SubMesh> m_SubMeshes;
	};
}
