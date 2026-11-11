#include "CNglTFImporter.h"
#include <cstdio>
#include <rapidjson/filereadstream.h>
#include <filesystem>
#include "iostream"

using namespace rapidjson;

namespace 
{
	std::filesystem::path ToPath(const std::string& utf8)
	{
		return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
	}
}

bool CNglTFImporter::ReadglTF(std::string filePath, std::string binFilePath, CNglTF& cNglTF)
{
	//***** glTF -> Json -> rapidjson::doc 변환
	FILE* file = nullptr;
	if(_wfopen_s(&file, ToPath(filePath).c_str(), L"rt") != 0)
		return false;

	char buff[2048] = "";
	FileReadStream frs(file, buff, sizeof(buff));
	cNglTF.m_doc.ParseStream(frs);
	fclose(file);


	//***** bin -> vector<uint8_t> 변환
	FILE* binFile = nullptr;

	if (_wfopen_s(&binFile, ToPath(binFilePath).c_str(), L"rb") != 0)
		return false;

	//*** 파일 전체 크기 확인
	std::fseek(binFile, 0, SEEK_END); // fseek 현재 파일의 읽기, 쓰기 위치를 파일 끝으로 이동
	long fileSize = std::ftell(binFile); // 현재 읽기, 쓰기 위치를 long 형으로 반환
	if (fileSize <= 0) // 파일 사이즈 확인을 실패하거나 파일 사이즈가 0 바이트 이면 false
	{
		std::fclose(binFile);
		return false;
	}


	//*** 파일 크기만큼 vector 할당 후 데이터 복사
	cNglTF.m_bin.resize(static_cast<size_t>(fileSize));
	std::fseek(binFile, 0, SEEK_SET); // 복사를 위해 파일 읽기, 쓰기 위치를 처음으로이동
	size_t readSize = std::fread(cNglTF.m_bin.data(), 1, cNglTF.m_bin.size(), binFile); // 현재 읽기 쓰기 위치부터 1바이트씩 바이너리 파일 사이즈만큼 읽어서 복사
	std::fclose(binFile);

	if (readSize != cNglTF.m_bin.size())
		return false;

	//***** 기본 데이터 추출 및 저장
	Value& doc = cNglTF.m_doc;
	Value& meshes = doc["meshes"];
	Value& accessors = doc["accessors"];
	Value& materials = doc["materials"];
	Value& nodes = doc["nodes"];

	//*** 메쉬 개수 저장
	cNglTF.m_meshCnt = meshes.Size(); 

	//*** 인덱스 개수 저장
	uint32_t polygonIdxCount = 0;
	for (Value& mesh : meshes.GetArray())
	{
		for (Value& primitive : mesh["primitives"].GetArray())
		{
			uint32_t index = primitive["indices"].GetUint();
			uint32_t count = accessors.GetArray()[index]["count"].GetUint();
			cNglTF.m_idxVtxCnt += count;
			if (!primitive.HasMember("mode") || primitive["mode"].GetUint() == 4) // 폴리곤 개수를 샐 때 삼각형 그리기에 사용되는 인덱스를 따로 더해준다.
			{
				polygonIdxCount += count;
			}

		}
	}
																
	//*** 폴리곤 개수 저장
	cNglTF.m_faceCnt = polygonIdxCount / 3;

	//*** 머터리얼 개수 저장
	cNglTF.m_materialCnt = materials.Size();

	//*** 노드 개수 저장
	cNglTF.m_nodeCnt = nodes.Size();

	return true;
}
