#pragma once
#include <string>
#include <vector>
#include <memory>

#include <rapidjson/document.h>
#include "Math/Matrix4.h"
#include "Asset/Mesh/MeshAsset.h"

namespace Canape
{
	struct CNglTF
	{
		std::string m_filePath;
		std::string m_binFIlePath;
		rapidjson::Document m_doc;
		std::vector<uint8_t> m_bin;

		uint32_t m_vtxCnt = 0; // 모델 전체 정점 개수
		uint32_t m_idxVtxCnt = 0; // IB를 사용하여 그릴 총 정점 개수
		uint32_t m_faceCnt = 0; // IB를 사용하여 그릴 총 삼각형 개수
		uint32_t m_materialCnt = 0; // 머터리얼 개수
		uint32_t m_nodeCnt = 0; // 노드 개수
		uint32_t m_meshCnt = 0; // 메쉬 개수
	};

	class TextureAsset;
	class AnimationClipAsset;
	class GameObject;
	class Scene;

	class CNglTFImporter
	{
	public:
		bool ReadglTF(std::string filePath, CNglTF& cNglTF);
		std::unique_ptr<MeshAsset> LoadMeshAsset(CNglTF& cNglTF);
		std::unique_ptr<TextureAsset> LoadTextureAsset(CNglTF& cNglTF);
		std::vector<std::unique_ptr<AnimationClipAsset>> LoadAnimationClipAsset(CNglTF& cNglTF);
		std::unique_ptr<MeshAsset> LoadSkinnedMeshAsset(CNglTF& cNglTF, uint32_t skinIndex);
		// 노드 하나에 붙은 (스킨이 아닌) 메쉬를 그 노드의 로컬 공간으로 읽는다. 자식 노드는 포함하지 않는다.
		// 애니메이션되는 노드에 달린 강체 메쉬(풍차 날개 등)를 노드 오브젝트의 MeshRenderer로 그릴 때 쓴다("#node<인덱스>").
		std::unique_ptr<MeshAsset> LoadNodeMeshAsset(CNglTF& cNglTF, uint32_t nodeIndex);
		GameObject* LoadSkeleton(CNglTF& cNglTF, Scene& scene, const std::string& name, std::vector<GameObject*>& nodeObjects);
		bool ImportSkeleton(const std::string& gltfPath, const std::string& targetDir, const std::string& clipPath = "");
		// 노드 계층을 오브젝트로 만들고, 스킨 메쉬는 SkinnedMeshRenderer로, 노드에 달린 일반 메쉬는 그 노드의 MeshRenderer로 붙인다.
		bool ImportSkeletonModel(const std::string& gltfPath, const std::string& targetDir, const std::string& clipPath = "");
		// 움직이지 않는 모델: 파일을 targetDir로 복사하고, 모델 전체를 한 메쉬로 그리는 MeshRenderer 하나짜리 프리팹을 만든다.
		bool ImportStaticModel(const std::string& gltfPath, const std::string& targetDir);

	private:
		// glTF와 bin, 이미지들을 targetDir로 복사하고 복사된 glTF 경로를 돌려준다(실패 시 빈 문자열).
		std::string CopyGltfFiles(const std::string& gltfPath, const std::string& targetDir);
		void LoadMaterials(CNglTF& cNglTF, std::vector<MaterialData>& materialSlots);
		Matrix4 GetNodeTRS(rapidjson::Value& node);
		void LoadNodeMesh(
			CNglTF& cNglTF,
			rapidjson::Value& node,
			Matrix4& parentTM,
			std::vector<StaticVertex>& vertices,
			std::vector<uint32_t>& indices,
			std::vector<SubMesh>& subMeshes,
			bool includeNodeTransform = true, // false면 노드 자신의 TRS를 적용하지 않는다(노드 로컬 공간)
			bool recurse = true               // false면 자식 노드로 내려가지 않는다
		);

	};
	void BuildPaths(CNglTF& cNglTF, uint32_t currentNodeIndex, const std::string& parentPath, std::vector<std::string>& out);
}
