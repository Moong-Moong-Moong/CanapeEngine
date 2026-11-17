#pragma once

#include "Asset/AssetId.h"
#include "DX11Renderer/Data/MeshData.h"
#include "Math/Vector3.h"
#include "../Material/MaterialData.h"
#include <cstdint>
#include <vector>
#include "Math/Matrix4.h"

namespace Canape
{
	class MeshAsset
	{
	public:
		MeshAsset() = default;
		MeshAsset(const MeshAsset&) = delete;
		MeshAsset& operator=(const MeshAsset&) = delete;
		MeshAsset(std::vector<Vector3> positions, std::vector<uint32_t> indices);
		MeshAsset(
			std::vector<StaticVertex> vertices, 
			std::vector<uint32_t> indices, 
			std::vector<SubMesh> submeshes,
			std::vector<MaterialData> materialSlots = {} // 머티리얼 정보가 없는 메쉬(FBX, Builtin)는 비워둔다.
		);
		MeshAsset(
			std::vector<SkinnedVertex> vertices,
			std::vector<uint32_t> indices,
			std::vector<SubMesh> submeshes,
			std::vector<Matrix4> inverseBindMatrices,   // joint 수만큼
			std::vector<std::string> bonePaths,         // joint j의 노드 경로 (모델 루트 기준)
			std::vector<MaterialData> materialSlots = {});


		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
		const std::vector<StaticVertex>& GetVertices() const { return m_Vertices; }
		const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }
		// SubMesh::materialSlot가 이 배열의 인덱스. name에는 기본 머티리얼(.mat) 에셋 경로가 들어 있다.
		const std::vector<MaterialData>& GetMaterialSlots() const { return m_materialSlots; }

		VertexFormat GetVertexFormat() const { return m_Format; }

		uint32_t GetVertexCount() const;
		uint32_t GetIndexCount() const { return static_cast<uint32_t>(m_Indices.size()); }

		const std::vector<SkinnedVertex>& GetSkinnedVertices() const { return m_SkinnedVertices; }
		const std::vector<Matrix4>& GetInverseBindMatrices() const { return m_InverseBindMatrices; }
		const std::vector<std::string>& GetBonePaths() const { return m_BonePaths; }
		uint32_t GetBoneCount() const { return static_cast<uint32_t>(m_BonePaths.size()); }

		MeshAssetId GetAssetId() const { return m_AssetId; }

	private:
		void InitializeSubMeshes();
		static MeshAssetId AllocateAssetId();
		VertexFormat m_Format = VertexFormat::Static;
		const MeshAssetId m_AssetId = AllocateAssetId();
		std::vector<StaticVertex> m_Vertices;
		std::vector<SkinnedVertex> m_SkinnedVertices;
		std::vector<Matrix4> m_InverseBindMatrices;
		std::vector<std::string> m_BonePaths;
		std::vector<uint32_t> m_Indices;
		std::vector<SubMesh> m_SubMeshes;
		std::vector<MaterialData> m_materialSlots;
	};
}
