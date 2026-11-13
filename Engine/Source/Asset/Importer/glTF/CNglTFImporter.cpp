#include "CNglTFImporter.h"
#include <cstdio>
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
		//***** 머터리얼 슬롯 추출
		std::vector<MaterialData> materialSlots;
		LoadMaterials(cNglTF, materialSlots);
		
		//***** 노드 데이터 추출
		Value& doc = cNglTF.m_doc;
		if (!doc.HasMember("scene")) // scene이 없는 glTF는 지원하지 않음
			return std::make_unique<MeshAsset>();
		uint32_t defaultSceneIndex = doc["scene"].GetUint();
		Value& scene = doc["scenes"][defaultSceneIndex];
		if (!scene.HasMember("nodes")) // 노드가 없으면 모델이 로드되지 않음
			return std::make_unique<MeshAsset>();
		
		//***** 정점 데이터 추출을 위한 데이터
		std::vector<StaticVertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<SubMesh> subMeshes;

		Value& rootNodes = scene["nodes"];
		for (Value& nodeIndex : rootNodes.GetArray())
		{
			//*** 노드에 메쉬가 있다면 정점 데이터 추출
			Matrix4 TRS = Matrix4::Identity; // 루트 노드의 부모 TRS는 단위행렬
			LoadNodeMesh(
				cNglTF,
				doc["nodes"][nodeIndex.GetUint()],
				TRS,
				vertices,
				indices,
				subMeshes
			);
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

	void CNglTFImporter::LoadMaterials(CNglTF& cNglTF, std::vector<MaterialData>& materialSlots)
	{
		Value& doc = cNglTF.m_doc;

		//**** 머터리얼 추출
		if (!doc.HasMember("materials"))
			return;

		Value& materials = doc["materials"];
		materialSlots.resize(materials.Size()); // 매터리얼 개수만큼 메쉬의 머터리얼 슬롯 공간 확보

		//*** 이 모델에서 사용하는 모든 머터리얼 추출
		for (uint32_t i = 0; i < materials.Size(); i++)
		{
			Value& material = materials[i];

			//*** 머터리얼 이름으로 머터리얼 에셋 경로 저장
			//* 머터리얼 이름이 있다면 사용, 없다면 glTF 파일 이름 + Material_i 사용
			if (material.HasMember("name"))
			{
				materialSlots[i].name = MakeAssetPath(cNglTF.m_filePath, std::string(material["name"].GetString()) + ".mat");
			}
			else
			{
				std::string fileStem = ToUtf8(ToPath(cNglTF.m_filePath).stem().u8string());
				materialSlots[i].name = MakeAssetPath(cNglTF.m_filePath, fileStem + "_Material_" + std::to_string(i) + ".mat"); 
			}

			//*** doubleSided 양면 렌더
			if (material.HasMember("doubleSided"))
			{
				materialSlots[i].doubleSided = material["doubleSided"].GetBool();
			}

			//*** pprMetallicRoughness 옵션
			if (material.HasMember("pbrMetallicRoughness"))
			{
				Value& pbrMetallicRoughness = material["pbrMetallicRoughness"];

				//* metallicFactor
				if (pbrMetallicRoughness.HasMember("metallicFactor"))
				{
					materialSlots[i].desc.metallic = material["pbrMetallicRoughness"]["metallicFactor"].GetFloat();
				}
				else
				{
					materialSlots[i].desc.metallic = 1.0f;
				}

				//* roughnessFactor
				if (pbrMetallicRoughness.HasMember("roughnessFactor"))
				{
					materialSlots[i].desc.roughness = material["pbrMetallicRoughness"]["roughnessFactor"].GetFloat();
				}
				else
				{
					materialSlots[i].desc.roughness = 1.0f;
				}

				//* baseColorFactor
				if (pbrMetallicRoughness.HasMember("baseColorFactor"))
				{
					materialSlots[i].desc.baseColor =
					{
						material["pbrMetallicRoughness"]["baseColorFactor"][0].GetFloat(),
						material["pbrMetallicRoughness"]["baseColorFactor"][1].GetFloat(),
						material["pbrMetallicRoughness"]["baseColorFactor"][2].GetFloat(),
						material["pbrMetallicRoughness"]["baseColorFactor"][3].GetFloat()
					};
				}

				//* baseColorTexture
				if (pbrMetallicRoughness.HasMember("baseColorTexture"))
				{
					uint32_t baseIndex = material["pbrMetallicRoughness"]["baseColorTexture"]["index"].GetUint();
					uint32_t sourceIndex = doc["textures"][baseIndex]["source"].GetUint();
					materialSlots[i].baseColorTexture = MakeAssetPath(cNglTF.m_filePath, doc["images"][sourceIndex]["uri"].GetString()); // glTF 파일과 같은 경로에 텍스처가 있다고 가정하고 경로지정
				}

				//* metallicRoughnessTexture
				if (pbrMetallicRoughness.HasMember("metallicRoughnessTexture"))
				{
					uint32_t metallicRoughnessIndex = material["pbrMetallicRoughness"]["metallicRoughnessTexture"]["index"].GetUint();
					uint32_t sourceIndex = doc["textures"][metallicRoughnessIndex]["source"].GetUint();
					materialSlots[i].metallicRoughnessTexture = MakeAssetPath(cNglTF.m_filePath, doc["images"][sourceIndex]["uri"].GetString()); // glTF 파일과 같은 경로에 텍스처가 있다고 가정하고 경로지정
				}
			}

			if (material.HasMember("normalTexture"))
			{
				uint32_t normalIndex = material["normalTexture"]["index"].GetUint();
				uint32_t sourceIndex = doc["textures"][normalIndex]["source"].GetUint();
				materialSlots[i].normalTexture = MakeAssetPath(cNglTF.m_filePath, doc["images"][sourceIndex]["uri"].GetString()); // glTF 파일과 같은 경로에 텍스처가 있다고 가정하고 경로지정
			}

			if (material.HasMember("occlusionTexture"))
			{
				uint32_t occlusionIndex = material["occlusionTexture"]["index"].GetUint();
				uint32_t sourceIndex = doc["textures"][occlusionIndex]["source"].GetUint();
				materialSlots[i].occlusionTexture = MakeAssetPath(cNglTF.m_filePath, doc["images"][sourceIndex]["uri"].GetString()); // glTF 파일과 같은 경로에 텍스처가 있다고 가정하고 경로지정
			}

			if (material.HasMember("emissiveTexture"))
			{
				uint32_t emissiveIndex = material["emissiveTexture"]["index"].GetUint();
				uint32_t sourceIndex = doc["textures"][emissiveIndex]["source"].GetUint();
				materialSlots[i].emissiveTexture = MakeAssetPath(cNglTF.m_filePath, doc["images"][sourceIndex]["uri"].GetString()); // glTF 파일과 같은 경로에 텍스처가 있다고 가정하고 경로지정
			}
		}

		return;
	}

	Matrix4 CNglTFImporter::GetNodeTRS(Value& node)
	{
		Matrix4 s;
		Matrix4 r;
		Matrix4 t;
		if (node.HasMember("rotation"))
		{
			Value& rotation = node["rotation"];
			r = Matrix4::Rotation(
				{
					rotation[0].GetFloat(),
					rotation[1].GetFloat(),
					rotation[2].GetFloat(),
					rotation[3].GetFloat(),
				}
				);
		}

		if (node.HasMember("scale"))
		{
			Value& scale = node["scale"];
			s = Matrix4::Scale(
				{
					scale[0].GetFloat(),
					scale[1].GetFloat(),
					scale[2].GetFloat(),
				}
				);
		}

		if (node.HasMember("translation"))
		{
			Value& translation = node["translation"];
			t = Matrix4::Translation(
				{
					translation[0].GetFloat(),
					translation[1].GetFloat(),
					translation[2].GetFloat(),
				}
				);
		}

		return s * r * t;
	}

	void CNglTFImporter::LoadNodeMesh(
		CNglTF& cNglTF, 
		Value& node, 
		Matrix4& parentTM,
		std::vector<StaticVertex>& vertices,
		std::vector<uint32_t>& indices,
		std::vector<SubMesh>& subMeshes
	)
	{
		Value& doc = cNglTF.m_doc;

		//*** 노드 로컬 tm 행렬
		Matrix4 TRS = GetNodeTRS(node) * parentTM; // 우리 엔진은 행벡터 규약을 사용

		//*** 행렬식: 음수면 노드에 미러링(음수 스케일)이 있음 → 와인딩 판단용
		float det = DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(TRS.ToXM()));

		//*** 노드에 메쉬가 있다면 메쉬 정보 추출
		if (node.HasMember("mesh"))
		{
			uint32_t meshIndex = node["mesh"].GetUint();
			Value& mesh = doc["meshes"][meshIndex];

			for (Value& primitive : mesh["primitives"].GetArray())
			{
				//***** 서브메쉬 정보 넣기 - 1
				SubMesh sm;
				sm.startIndex = indices.size();  // 이 primitve의 인덱스 넣기전 시작점
				sm.baseVertex = vertices.size(); //     ``        정점 넣기전 시작점

				if (primitive.HasMember("material")) // 머터리얼을 사용한다면
				{
					sm.materialSlot = primitive["material"].GetUint();
				}

				//***** Vertex 정보 추출 : position, normal, uv, tangent
				Value& attributes = primitive["attributes"];
				Value& accessors = doc["accessors"];
				Value& bufferViews = doc["bufferViews"];

				//*** Position
				uint32_t aPos = attributes["POSITION"].GetUint(); // 포지션 엑세서 인덱스
				uint32_t bPos = accessors[aPos]["bufferView"].GetUint(); // 포지션 버퍼뷰 인덱스
				uint32_t posCnt = accessors[aPos]["count"].GetUint(); // 읽을 포지션 개수
				uint32_t pOffset = bufferViews[bPos]["byteOffset"].GetUint(); // 포지션 버퍼 뷰 오프셋

				//***** 바이너리에서 정점 정보 복사
				//*** 이 primitive를 위한 정점 데이터 메모리 할당
				uint32_t svStart = static_cast<uint32_t>(vertices.size());
				vertices.resize(svStart + posCnt);

				for (uint32_t i = 0; i < posCnt; i++)
				{
					Vector3 pos;
					std::memcpy(&pos, cNglTF.m_bin.data() + pOffset + (i * sizeof(Vector3)), sizeof(Vector3));
					pos = TRS.TransformPoint(pos);
					// 좌표계 변환
					pos.X = -pos.X;
					vertices[svStart + i].Position = pos;
				}

				//*** Normal
				if (attributes.HasMember("NORMAL"))
				{
					uint32_t aNormal = attributes["NORMAL"].GetUint(); // 노말 엑세서 인덱스
					uint32_t bNormal = accessors[aNormal]["bufferView"].GetUint(); // 노말 버퍼뷰 인덱스
					uint32_t norCnt = accessors[aNormal]["count"].GetUint(); // 읽을 노말 개수
					uint32_t nOffset = bufferViews[bNormal]["byteOffset"].GetUint(); // 노말 버퍼 뷰 오프셋

					//* Normal에는 스케일이 포함되면 방향이 바뀌기 때문에 역전체 행렬을 사용해야함
					DirectX::XMMATRIX normalMat = DirectX::XMMatrixTranspose(TRS.Inverse().ToXM());
					for (uint32_t i = 0; i < norCnt; i++)
					{
						Vector3 normal;
						std::memcpy(&normal, cNglTF.m_bin.data() + nOffset + (i * sizeof(Vector3)), sizeof(Vector3));
						DirectX::XMVECTOR n =
							DirectX::XMVector3TransformNormal(normal.ToXM(), normalMat);
						Vector3 nrm = Vector3::FromXM(DirectX::XMVector3Normalize(n));
						nrm.X = -nrm.X; // 좌표계 변환
						vertices[svStart + i].Normal = nrm;
					}
				}

				//*** uv
				if (attributes.HasMember("TEXCOORD_0"))
				{
					uint32_t aUv = attributes["TEXCOORD_0"].GetUint(); // UV 엑세서 인덱스
					uint32_t bUv = accessors[aUv]["bufferView"].GetUint(); // UV 버퍼뷰 인덱스
					uint32_t uvCnt = accessors[aUv]["count"].GetUint(); // 읽을 UV 개수
					uint32_t uvOffset = bufferViews[bUv]["byteOffset"].GetUint(); // UV 버퍼 뷰 오프셋

					for (uint32_t i = 0; i < uvCnt; i++)
					{
						Vector2 uv;
						std::memcpy(&uv, cNglTF.m_bin.data() + uvOffset + (i * sizeof(Vector2)), sizeof(Vector2));
						vertices[svStart + i].UV = uv;
					}
				}

				//*** 탄젠트
				bool hasTangent = attributes.HasMember("TANGENT");
				if (hasTangent)
				{
					uint32_t aTan = attributes["TANGENT"].GetUint();
					uint32_t bTan = accessors[aTan]["bufferView"].GetUint();
					uint32_t tanCnt = accessors[aTan]["count"].GetUint();
					uint32_t tOffset = bufferViews[bTan]["byteOffset"].GetUint();
					const DirectX::XMMATRIX M = TRS.ToXM();

					for (uint32_t i = 0; i < tanCnt; i++)
					{
						Vector4 t; // VEC4 float
						std::memcpy(&t, cNglTF.m_bin.data() + tOffset + (i * sizeof(Vector4)), sizeof(Vector4));

						// 탄젠트는 표면 방향 → 역전치 X, TRS 그대로 w=0
						Vector3 tv = Vector3::FromXM(DirectX::XMVector3Normalize(
							DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(t.X, t.Y, t.Z, 0.0f), M)));
						tv.X = -tv.X;                              // 좌표계 미러링
						const float w = (det > 0.0f) ? -t.W : t.W; // 손 방향이 뒤집히면 부호 반전
						vertices[svStart + i].Tangent = { tv.X, tv.Y, tv.Z, w };
					}
				}

				//***** 인덱스 복사
				uint32_t aIndex = primitive["indices"].GetUint();
				uint32_t bIndex = accessors[aIndex]["bufferView"].GetUint();
				uint32_t iOffset = bufferViews[bIndex]["byteOffset"].GetUint();
				uint32_t indexCnt = accessors[aIndex]["count"].GetUint();
				const uint32_t iStart = static_cast<uint32_t>(indices.size());  // = sm.startIndex
				indices.resize(iStart + indexCnt);
				for (uint32_t i = 0; i < indexCnt; i++)
				{
					uint16_t vi; // componentType : 5123 - 2byte; 인덱스는 2바이트
					std::memcpy(&vi, cNglTF.m_bin.data() + iOffset + (i * sizeof(uint16_t)), sizeof(uint16_t));
					indices[iStart + i] = vi;
				}

				//***** 와인딩 반전: X 미러링으로 앞뒷면이 바뀜 (노드에도 미러링이 있으면 원래대로)
				if (det > 0.0f)
				{
					for (uint32_t i = 0; i + 2 < indexCnt; i += 3)
						std::swap(indices[iStart + i + 1], indices[iStart + i + 2]);
				}

				//***** 탄젠트 계산 (정점, 인덱스가 모두 채워진 뒤에 해야 함)
				if (!hasTangent)
				{
					ComputeTangents(vertices, svStart, posCnt, indices, iStart, indexCnt);
				}

				//***** 서브메쉬 정보 넣기 - 2
				sm.indexCount = indexCnt; // 인덱스 값 몇개 넣었는지 저장
				subMeshes.push_back(sm);
			}
		}

		//***** 자식노드로 재귀
		if (node.HasMember("children"))
		{
			for (Value& child : node["children"].GetArray())
			{
				uint32_t childNodeIndex = child.GetUint();
				Value& childNode = cNglTF.m_doc["nodes"][childNodeIndex];

				LoadNodeMesh(
					cNglTF,
					childNode,
					TRS,
					vertices,
					indices,
					subMeshes
				);
			}
		}
	}


}


