#pragma once

#include "MeshAssetId.h"
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
		}

		const std::vector<Vector3>& GetPositions() const { return m_Positions; }
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

		uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_Positions.size()); }
		uint32_t GetIndexCount() const { return static_cast<uint32_t>(m_Indices.size()); }

		MeshAssetId GetAssetId() const { return m_AssetId; }

	private:
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
		std::vector<uint32_t> m_Indices;
	};
}
