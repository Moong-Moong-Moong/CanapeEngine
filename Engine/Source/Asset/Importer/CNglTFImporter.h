#include <rapidjson/document.h>
#include <string>
#include <vector>

class CNglTF
{
public:

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

class CNglTFImporter
{
public:
	// glTF, bin 파일을 읽고 cNglTF 데이터를 채운다.
	bool ReadglTF(std::string filePath, std::string binFilePath, CNglTF& cNglTF);
};