#pragma once

#include "Math/Vector3.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace Canape
{
	class Mesh
	{
	public:
		Mesh() = default;
		Mesh(std::vector<Vector3> positions, std::vector<uint32_t> indices)
			: m_Positions(std::move(positions))
			, m_Indices(std::move(indices))
		{
		}

		const std::vector<Vector3>& GetPositions() const { return m_Positions; }
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

		uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_Positions.size()); }
		uint32_t GetIndexCount() const { return static_cast<uint32_t>(m_Indices.size()); }

		uint32_t GetGpuHandle() const { return m_GpuHandle; }
		void SetGpuHandle(uint32_t handle) { m_GpuHandle = handle; }

	private:
		std::vector<Vector3> m_Positions;
		std::vector<uint32_t> m_Indices;
		uint32_t m_GpuHandle = 0;
	};
}
