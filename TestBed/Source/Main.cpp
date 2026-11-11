#include "Asset/Importer/CNglTFImporter.h"

#include <cstdio>
#include "Main.h"

int main()
{
	//TestImportGlTF();

	return 0;
}

void TestImportGlTF()
{
	CNglTF gltf{};
	CNglTFImporter importer;
	const bool ok = importer.ReadglTF("Data/glTF/CastleGuard01.gltf", "Data/glTF/CastleGuard01.bin", gltf);

	std::printf("ReadglTF : %s\n", ok ? "true" : "false");
	std::printf("bin      : %zu bytes\n", gltf.m_bin.size());
	std::printf("meshes   : %u\n", gltf.m_meshCnt);
	std::printf("nodes    : %u\n", gltf.m_nodeCnt);
	std::printf("materials: %u\n", gltf.m_materialCnt);
	std::printf("indices  : %u\n", gltf.m_idxVtxCnt);
	std::printf("faces    : %u\n", gltf.m_faceCnt);
}


