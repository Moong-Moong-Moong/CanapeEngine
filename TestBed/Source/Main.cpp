#include "Asset/Importer/glTF/CNglTFImporter.h"
#include "Asset/Mesh/MeshAsset.h"

#include <cstdio>
#include <exception>
#include "Main.h"

using namespace Canape;

int main()
{
	//TestImportGlTF();
	TestLoadMeshAsset();

	return 0;
}

void TestImportGlTF()
{
	CNglTF gltf{};
	CNglTFImporter importer;
	const bool ok = importer.ReadglTF("Data/glTF/CastleGuard01.gltf", gltf);

	std::printf("ReadglTF : %s\n", ok ? "true" : "false");
	std::printf("bin path : %s\n", gltf.m_binFIlePath.c_str());
	std::printf("bin      : %zu bytes\n", gltf.m_bin.size());
	std::printf("meshes   : %u\n", gltf.m_meshCnt);
	std::printf("nodes    : %u\n", gltf.m_nodeCnt);
	std::printf("materials: %u\n", gltf.m_materialCnt);
	std::printf("indices  : %u\n", gltf.m_idxVtxCnt);
	std::printf("faces    : %u\n", gltf.m_faceCnt);
}

void TestLoadMeshAsset()
{
	const char* gltfPath = "Data/glTF/CastleGuard01.gltf";
	const char* binPath = "Data/glTF/CastleGuard01.bin";

	CNglTF gltf{};
	CNglTFImporter importer;
	if (!importer.ReadglTF(gltfPath, gltf))
	{
		std::printf("ReadglTF 실패\n");
		return;
	}

	std::unique_ptr<MeshAsset> mesh;
	try
	{
		mesh = importer.LoadMeshAsset(gltf);
	}
	catch (const std::exception& e) // MeshAsset 생성자의 ValidateMeshIndices 실패
	{
		std::printf("LoadMeshAsset 예외: %s\n", e.what());
		return;
	}

	if (!mesh)
	{
		std::printf("LoadMeshAsset: nullptr\n");
		return;
	}

	std::printf("vertices : %u\n", mesh->GetVertexCount());
	std::printf("indices  : %u (glTF: %u)\n", mesh->GetIndexCount(), gltf.m_idxVtxCnt);
	std::printf("submeshes: %zu\n", mesh->GetSubMeshes().size());
	for (size_t i = 0; i < mesh->GetSubMeshes().size(); ++i)
	{
		const SubMesh& sm = mesh->GetSubMeshes()[i];
		std::printf("  [%zu] start %u, count %u, baseVertex %d, material %u\n",
			i, sm.startIndex, sm.indexCount, sm.baseVertex, sm.materialSlot);
	}

	// 앞쪽 정점 몇 개를 찍어서 값이 정상인지 눈으로 확인
	const auto& vertices = mesh->GetVertices();
	for (size_t i = 0; i < vertices.size() && i < 3; ++i)
	{
		const StaticVertex& v = vertices[i];
		std::printf("  v%zu pos(%.3f, %.3f, %.3f) nor(%.3f, %.3f, %.3f) uv(%.3f, %.3f)\n", i,
			v.Position.X, v.Position.Y, v.Position.Z, v.Normal.X, v.Normal.Y, v.Normal.Z, v.UV.X, v.UV.Y);
	}
}
