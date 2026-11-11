#include "CNglTFImporter.h"
#include <cstdio>
#include <rapidjson/filereadstream.h>
#include <filesystem>
#include <cstring>
#include "../../Mesh/MeshAsset.h"

using namespace rapidjson;

namespace Canape
{
	namespace
	{
		std::filesystem::path ToPath(const std::string& utf8)
		{
			return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
		}
	}

	bool CNglTFImporter::ReadglTF(std::string filePath, CNglTF& cNglTF)
	{
		//***** glTF -> Json -> rapidjson::doc 변환
		FILE* file = nullptr;
		if (_wfopen_s(&file, ToPath(filePath).c_str(), L"rt") != 0)
			return false;

		char buff[2048] = "";
		FileReadStream frs(file, buff, sizeof(buff));
		cNglTF.m_doc.ParseStream(frs);
		fclose(file);


		//***** bin 경로 결정: glTF와 같은 디렉토리 + buffers[0].uri (파일 이름)
		const std::filesystem::path binPath = ToPath(filePath).parent_path() / ToPath(cNglTF.m_doc["buffers"][0]["uri"].GetString());
		const std::u8string binPathU8 = binPath.generic_u8string();
		cNglTF.m_filePath = filePath;
		cNglTF.m_binFIlePath = std::string(binPathU8.begin(), binPathU8.end());


		//***** bin -> vector<uint8_t> 변환
		FILE* binFile = nullptr;

		if (_wfopen_s(&binFile, binPath.c_str(), L"rb") != 0)
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
	std::unique_ptr<MeshAsset> CNglTFImporter::LoadMeshAsset(CNglTF& cNglTF)
	{
		std::vector<StaticVertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<SubMesh> subMeshes;

		Value& doc = cNglTF.m_doc;

		if (!doc.HasMember("meshes"))
			return nullptr;

		Value& meshes = doc["meshes"];
		Value& accessors = doc["accessors"];
		Value& bufferView = doc["bufferViews"];

		for (Value& mesh : meshes.GetArray())
		{	
			for (Value& primitive : mesh["primitives"].GetArray())
			{
				//***** 서브메쉬 정보 넣기 - 1
				SubMesh sm;
				sm.startIndex = indices.size(); // 이 primitve에 인덱스 넣기전 개수
				sm.baseVertex = vertices.size(); // `` 정점 넣기 전 개수

				//***** 버텍스 포지션, 노말, uv, tangent
				//*** primitive 별 position, normal, uv 엑세서 인덱스
				Value& attributes = primitive["attributes"];
				uint32_t aPos = attributes["POSITION"].GetUint();
				uint32_t aNormal = attributes["NORMAL"].GetUint();
				uint32_t aUv = attributes["TEXCOORD_0"].GetUint();

				//*** 버퍼 뷰 인덱스
				uint32_t bPos = accessors[aPos]["bufferView"].GetUint();
				uint32_t posCnt = accessors[aPos]["count"].GetUint(); // 읽을 포지션 개수

				uint32_t bNormal = accessors[aNormal]["bufferView"].GetUint();
				uint32_t norCnt = accessors[aNormal]["count"].GetUint(); // 읽을 노말 개수

				uint32_t bUv = accessors[aUv]["bufferView"].GetUint();
				uint32_t uvCnt = accessors[aUv]["count"].GetUint(); // 읽을 uv 개수

				//*** 바이너리에서 정점 정보 복사
				uint32_t pOffset = bufferView[bPos]["byteOffset"].GetUint();
				uint32_t nOffset = bufferView[bNormal]["byteOffset"].GetUint();
				uint32_t uvOffset = bufferView[bUv]["byteOffset"].GetUint();

				uint32_t svStart = static_cast<uint32_t>(vertices.size());
				vertices.resize(svStart + posCnt);

				for (int i = 0; i < posCnt; i++)
				{
					StaticVertex v;
					std::memcpy(&v.Position, cNglTF.m_bin.data() + pOffset + (i * sizeof(Vector3)), sizeof(Vector3));
					std::memcpy(&v.Normal, cNglTF.m_bin.data() + nOffset + (i * sizeof(Vector3)), sizeof(Vector3));
					std::memcpy(&v.UV, cNglTF.m_bin.data() + uvOffset + (i * sizeof(Vector2)), sizeof(Vector2));
					vertices[svStart + i] = v;
				}

				//***** 인덱스 복사
				uint32_t aIndex = primitive["indices"].GetUint();
				uint32_t bIndex = accessors[aIndex]["bufferView"].GetUint();
				uint32_t iOffset = bufferView[bIndex]["byteOffset"].GetUint();
				uint32_t indexCnt = accessors[aIndex]["count"].GetUint();
				const uint32_t iStart = static_cast<uint32_t>(indices.size());  // = sm.startIndex
				indices.resize(iStart + indexCnt);
				for (int i = 0; i < indexCnt; i++)
				{
					uint16_t vi; // componentType : 5123 - 2byte; 인덱스는 2바이트
					std::memcpy(&vi, cNglTF.m_bin.data() + iOffset + (i * sizeof(uint16_t)), sizeof(uint16_t));
					indices[i] = vi;
				}

				//***** 서브메쉬 정보 넣기 - 2
				sm.indexCount = indexCnt;
				subMeshes.push_back(sm);
			}

		}

		return std::make_unique<MeshAsset>(std::move(vertices), std::move(indices), std::move(subMeshes));
	}
}


