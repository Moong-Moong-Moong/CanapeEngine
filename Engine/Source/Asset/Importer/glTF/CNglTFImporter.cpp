#include "CNglTFImporter.h"
#include <cstdio>
#include <rapidjson/filereadstream.h>
#include <filesystem>
#include <cstring>
#include <cmath>
#include <vector>
#include "../../Mesh/MeshAsset.h"
#include "../../Texture/TextureAsset.h"
#include "Asset/AssetManager.h"  

using namespace rapidjson;

namespace Canape
{
	namespace
	{
		std::filesystem::path ToPath(const std::string& utf8)
		{
			return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
		}

		std::string ToUtf8(const std::u8string& s) { return std::string(s.begin(), s.end()); }

		// glTF 디렉토리 + fileName → "Content/Models/xxx" 형태의 에셋 경로
		std::string MakeAssetPath(const std::string& gltfFile, const std::string& fileName)
		{
			const std::filesystem::path full = (ToPath(gltfFile).parent_path() / ToPath(fileName)).lexically_normal();
			return AssetManager::ToAssetPath(ToUtf8(full.generic_u8string()));
		}

		// ===== [Tangent] 헬퍼 시작 =====
		// UV가 없거나 퇴화해서 탄젠트를 구할 수 없을 때 쓰는 N에 수직인 임의 벡터.
		Vector3 AnyPerpendicular(const Vector3& normal)
		{
			const Vector3 axis = std::abs(normal.Y) < 0.99f ? Vector3::Up : Vector3::Right;
			return Vector3::Cross(axis, normal).Normalized();
		}

		// primitive 하나의 정점 탄젠트를 계산해서 vertices[vertexStart ~ vertexStart + vertexCount)에 저장한다.
		// glTF는 인덱스 버퍼로 삼각형을 구성하고, 같은 정점은 이미 공유되어 있으므로
		// 정점 하나 = 탄젠트 누적 단위다. (FBX처럼 논리 정점 그룹을 따로 만들 필요 없음)
		// indices[indexStart ~ indexStart + indexCount)는 이 primitive 기준 로컬 정점 번호다.
		// 셰이더에서는 B = cross(N, T.xyz) * T.w 를 사용한다.
		void ComputeTangents(std::vector<StaticVertex>& vertices, uint32_t vertexStart, uint32_t vertexCount,
			const std::vector<uint32_t>& indices, uint32_t indexStart, uint32_t indexCount)
		{
			std::vector<Vector3> tangents(vertexCount);   // 로컬 정점 번호별 누적 탄젠트
			std::vector<Vector3> bitangents(vertexCount); // 로컬 정점 번호별 누적 바이탄젠트

			//*** 1. 삼각형마다 탄젠트/바이탄젠트를 구해서 세 정점에 누적
			for (uint32_t i = 0; i + 2 < indexCount; i += 3)
			{
				const uint32_t i0 = indices[indexStart + i];
				const uint32_t i1 = indices[indexStart + i + 1];
				const uint32_t i2 = indices[indexStart + i + 2];
				if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
					continue; // 잘못된 인덱스 방어

				const StaticVertex& v0 = vertices[vertexStart + i0];
				const StaticVertex& v1 = vertices[vertexStart + i1];
				const StaticVertex& v2 = vertices[vertexStart + i2];

				// 위치 차이(e1, e2)와 UV 차이(du, dv)로 UV의 U/V 방향이 3D에서 어디를 향하는지 구한다.
				const Vector3 e1 = v1.Position - v0.Position;
				const Vector3 e2 = v2.Position - v0.Position;
				const float du1 = v1.UV.X - v0.UV.X, dv1 = v1.UV.Y - v0.UV.Y;
				const float du2 = v2.UV.X - v0.UV.X, dv2 = v2.UV.Y - v0.UV.Y;
				const float det = du1 * dv2 - du2 * dv1;
				if (std::abs(det) < 1e-12f)
					continue; // UV가 퇴화한 삼각형은 기여하지 않음

				const float r = 1.0f / det;
				const Vector3 t = (e1 * dv2 - e2 * dv1) * r; // U 방향
				const Vector3 b = (e2 * du1 - e1 * du2) * r; // V 방향
				for (uint32_t local : { i0, i1, i2 })
				{
					tangents[local] += t;
					bitangents[local] += b;
				}
			}

			//*** 2. 정점마다 노말에 수직이 되도록 보정(Gram-Schmidt)하고 방향 부호(w) 저장
			for (uint32_t local = 0; local < vertexCount; ++local)
			{
				StaticVertex& vertex = vertices[vertexStart + local];
				const Vector3& n = vertex.Normal;
				const Vector3& accumulated = tangents[local];
				Vector3 t = (accumulated - n * Vector3::Dot(n, accumulated)).Normalized();
				if (t.IsNearlyZero())
					t = AnyPerpendicular(n); // 노말이 0이면 탄젠트도 0으로 남음
				// UV가 미러링된 부분은 바이탄젠트 방향이 반대 → w = -1
				const float w = Vector3::Dot(Vector3::Cross(n, t), bitangents[local]) < 0.0f ? -1.0f : 1.0f;
				vertex.Tangent = { t.X, t.Y, t.Z, w };
			}
		}
		// ===== [Tangent] 헬퍼 끝 =====
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
		std::vector<MaterialData> materialSlots;

		Value& doc = cNglTF.m_doc;

		if (!doc.HasMember("meshes"))
			return nullptr;

		Value& meshes = doc["meshes"];
		Value& accessors = doc["accessors"];
		Value& bufferView = doc["bufferViews"];
		Value& textures = doc["textures"];
		Value& images = doc["images"];

		//**** 머터리얼 슬롯정보 저장
		Value& materials = doc["materials"];
		materialSlots.resize(materials.Size()); // 매터리얼 개수만큼 슬롯 공간 확보
		for (uint32_t i = 0; i < materials.Size(); i++)
		{
			Value& material = materials[i];
			materialSlots[i].name = MakeAssetPath(cNglTF.m_filePath, std::string(material["name"].GetString()) + ".mat");
			materialSlots[i].doubleSided = material["doubleSided"].GetBool();

			if (material.HasMember("pbrMetallicRoughness"))
			{
				materialSlots[i].desc.metallic = material["pbrMetallicRoughness"]["metallicFactor"].GetFloat();
				materialSlots[i].desc.roughness = material["pbrMetallicRoughness"]["roughnessFactor"].GetFloat();

				uint32_t baseIndex = material["pbrMetallicRoughness"]["baseColorTexture"]["index"].GetUint();
				uint32_t sourceIndex = textures[baseIndex]["source"].GetUint();
				materialSlots[i].baseColorTexture = MakeAssetPath(cNglTF.m_filePath, images[sourceIndex]["uri"].GetString());
				
				if (material["pbrMetallicRoughness"].HasMember("metallicRoughnessTexture"))
				{
					uint32_t metallicRoughnessIndex = material["pbrMetallicRoughness"]["metallicRoughnessTexture"]["index"].GetUint();
					sourceIndex = textures[metallicRoughnessIndex]["source"].GetUint();
					materialSlots[i].metallicRoughnessTexture = MakeAssetPath(cNglTF.m_filePath, images[sourceIndex]["uri"].GetString());
				}
			}

			if (material.HasMember("normalTexture"))
			{
				uint32_t normalIndex = material["normalTexture"]["index"].GetUint();
				uint32_t sourceIndex = textures[normalIndex]["source"].GetUint();
				materialSlots[i].normalTexture = MakeAssetPath(cNglTF.m_filePath, images[sourceIndex]["uri"].GetString());
			}

			if (material.HasMember("occlusionTexture"))
			{
				uint32_t occlusionIndex = material["occlusionTexture"]["index"].GetUint();
				uint32_t sourceIndex = textures[occlusionIndex]["source"].GetUint();
				materialSlots[i].occlusionTexture = MakeAssetPath(cNglTF.m_filePath, images[sourceIndex]["uri"].GetString());
			}

			if (material.HasMember("emissiveTexture"))
			{
				uint32_t emissiveIndex = material["emissiveTexture"]["index"].GetUint();
				uint32_t sourceIndex = textures[emissiveIndex]["source"].GetUint();
				materialSlots[i].emissiveTexture = MakeAssetPath(cNglTF.m_filePath, images[sourceIndex]["uri"].GetString());
			}
		}

		for (Value& mesh : meshes.GetArray())
		{	
			for (Value& primitive : mesh["primitives"].GetArray())
			{
				//***** 서브메쉬 정보 넣기 - 1
				SubMesh sm;
				sm.startIndex = indices.size(); // 이 primitve에 인덱스 넣기전 개수
				sm.baseVertex = vertices.size(); // `` 정점 넣기 전 개수
				if (primitive.HasMember("material"))
				{
					sm.materialSlot = primitive["material"].GetUint();
				}

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
					indices[iStart + i] = vi;
				}

				//***** 탄젠트 계산 (정점, 인덱스가 모두 채워진 뒤에 해야 함)
				ComputeTangents(vertices, svStart, posCnt, indices, iStart, indexCnt);

				//***** 서브메쉬 정보 넣기 - 2
				sm.indexCount = indexCnt;
				subMeshes.push_back(sm);
			}

		}

		return std::make_unique<MeshAsset>(std::move(vertices), std::move(indices), std::move(subMeshes), std::move(materialSlots));
	}
	std::unique_ptr<TextureAsset> CNglTFImporter::LoadTextureAsset(CNglTF& cNglTF)
	{
		Value& doc = cNglTF.m_doc;
		Value& textures = doc["textures"];
		Value& images = doc["images"];

		for (Value& texture : textures.GetArray())
		{
			uint32_t sourceIndex = texture["source"].GetUint();
			images[sourceIndex]["uri"].GetString();
		}

		return std::unique_ptr<TextureAsset>();
	}
}


