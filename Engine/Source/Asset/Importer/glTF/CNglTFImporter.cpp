#include "CNglTFImporter.h"
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <vector>
#include "../../Mesh/MeshAsset.h"
#include "../../Texture/TextureAsset.h"
#include "Asset/AssetManager.h"
#include <rapidjson/filereadstream.h>
#include "Scene/Scene.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/Component/SkinnedMeshRendererComponent.h"
#include "Scene/Component/AnimatorComponent.h"
#include "Scene/Serialization/SceneSerializer.h"

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

		// accessor가 가리키는 데이터의 bin 내 시작 위치 = bufferView.byteOffset + accessor.byteOffset (둘 다 생략 시 0)
		size_t GetAccessorDataOffset(Value& bufferViews, Value& accessor)
		{
			Value& bufferView = bufferViews[accessor["bufferView"].GetUint()];
			const size_t viewOffset = bufferView.HasMember("byteOffset") ? bufferView["byteOffset"].GetUint() : 0u;
			const size_t accessorOffset = accessor.HasMember("byteOffset") ? accessor["byteOffset"].GetUint() : 0u;
			return viewOffset + accessorOffset;
		}

		// 요소 간 간격. byteStride가 없으면 tight packing(요소 크기)
		size_t GetAccessorStride(Value& bufferViews, Value& accessor, size_t elementSize)
		{
			Value& bufferView = bufferViews[accessor["bufferView"].GetUint()];
			return bufferView.HasMember("byteStride") ? bufferView["byteStride"].GetUint() : elementSize;
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

	std::vector<std::unique_ptr<AnimationClipAsset>> CNglTFImporter::LoadAnimationClipAsset(CNglTF& cNglTF)
	{
		Value& doc = cNglTF.m_doc;
		if (!doc.HasMember("animations"))
		{
			return {};
		}

		//***** 본의 루트 기준 상대 경로를 구하기 위해 전체 노드 경로를 일괄 개산해놓는다.
		Value& nodes = doc["nodes"];
		std::vector<std::string> nodePaths(nodes.Size());
		for (Value& rootNode : doc["scenes"][0u]["nodes"].GetArray())
		{
			uint32_t rootNodeIndex = rootNode.GetUint();
			BuildPaths(cNglTF, rootNodeIndex, "", nodePaths);
		}

		Value& accessors = doc["accessors"];
		Value& bufferViews = doc["bufferViews"];

		Value& animations = doc["animations"];
		std::vector<std::unique_ptr<AnimationClipAsset>> clips;

		for (SizeType animIndex = 0; animIndex < animations.Size(); animIndex++)
		{
			Value& animation = animations[animIndex];
			Value& channels = animation["channels"];
			Value& samplers = animation["samplers"];

			std::vector<AnimationTrack> tracks;
			float duration = 0.0f;

			for (Value& channel : channels.GetArray())
			{
				AnimationTrack track;

				Value& target = channel["target"];
				Value& sampler = samplers[channel["sampler"].GetUint()];

				//*** 본 노드 경로
				track.targetPath = nodePaths[target["node"].GetUint()];

				//*** 트랙 프레임 정보 유형
				std::string property = target["path"].GetString();
				if (property == "translation") track.property = AnimationProperty::Translation;
				else if (property == "rotation") track.property = AnimationProperty::Rotation;
				else if (property == "scale") track.property = AnimationProperty::Scale;

				//*** 트랙 보간 방법
				std::string interpolation = sampler["interpolation"].GetString();
				if (interpolation == "STEP") track.interpolation = AnimationInterpolation::Step;
				else if (interpolation == "LINEAR") track.interpolation = AnimationInterpolation::Linear;

				//*** 키값 (시간)
				Value& aInput = accessors[sampler["input"].GetUint()];
				Value& aOutput = accessors[sampler["output"].GetUint()];
				uint32_t inputCount = aInput["count"].GetUint();
				uint32_t outputCount = aOutput["count"].GetUint();
				Value& bInput = bufferViews[aInput["bufferView"].GetUint()];
				Value& bOutput = bufferViews[aOutput["bufferView"].GetUint()];

				//*** 키 시간 읽기
				uint32_t inputOffset = bInput["byteOffset"].GetUint();
				track.times.resize(inputCount);
				for (uint32_t i = 0; i < inputCount; i++)
				{
					std::memcpy(&track.times[i], cNglTF.m_bin.data() + inputOffset + (i * sizeof(float)), sizeof(float));
				}

				//*** 키 값 읽기 (메쉬와 같은 엔진 좌표계로 X축 반전)
				uint32_t outputOffset = bOutput["byteOffset"].GetUint();
				track.values.resize(outputCount);
				for (uint32_t i = 0; i < outputCount; i++)
				{
					if (track.property == AnimationProperty::Rotation)
					{
						//* 회전 (x, y, z, w) → (x, -y, -z, w)
						Vector4 rotation;
						std::memcpy(&rotation, cNglTF.m_bin.data() + outputOffset + (i * sizeof(Vector4)), sizeof(Vector4));
						track.values[i] = { rotation.X, -rotation.Y, -rotation.Z, rotation.W };
					}
					else
					{
						//* 위치 (x, y, z) → (-x, y, z), 스케일은 그대로
						Vector3 value;
						std::memcpy(&value, cNglTF.m_bin.data() + outputOffset + (i * sizeof(Vector3)), sizeof(Vector3));
						if (track.property == AnimationProperty::Translation)
						{
							value.X = -value.X;
						}
						track.values[i] = { value.X, value.Y, value.Z, 0.0f };
					}
				}

				//*** 클립 길이 = 모든 트랙의 마지막 키 시간 중 최댓값 (시간은 오름차순)
				if (!track.times.empty() && track.times.back() > duration)
				{
					duration = track.times.back();
				}

				tracks.push_back(std::move(track));
			}

			std::string fileStem = ToUtf8(ToPath(cNglTF.m_filePath).stem().u8string());
			std::string name = animation.HasMember("name")
				? animation["name"].GetString()
				: fileStem + "_Anim_" + std::to_string(animIndex);
			clips.push_back(std::make_unique<AnimationClipAsset>(name, duration, std::move(tracks)));
		}

		return clips;
	}

	void BuildPaths(CNglTF& cNglTF, uint32_t currentNodeIndex, const std::string& parentPath, std::vector<std::string>& out)
	{
		Value& node = cNglTF.m_doc["nodes"][currentNodeIndex];

		std::string name;
		if (!node.HasMember("name"))
		{
			name = "Node_" + std::to_string(currentNodeIndex);
		}
		else
		{
			name = node["name"].GetString();
		}

		out[currentNodeIndex] = parentPath.empty() ? name : parentPath + "/" + name;

		if (!node.HasMember("children"))
			return;

		for (Value& child : node["children"].GetArray())
		{
			uint32_t childIndex = child.GetUint();
			BuildPaths(cNglTF, childIndex, out[currentNodeIndex], out);
		}
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
		//*** matrix가 있으면 TRS 대신 사용
		// glTF는 열벡터 + column-major 배열, 엔진은 행벡터 + row-major → 배열 순서 그대로 복사하면 된다.
		if (node.HasMember("matrix"))
		{
			Value& matrix = node["matrix"];
			Matrix4 m;
			for (uint32_t i = 0; i < 16; i++)
			{
				m.M[i / 4][i % 4] = matrix[i].GetFloat();
			}
			return m;
		}

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
				Value& aPos = accessors[attributes["POSITION"].GetUint()]; // 포지션 엑세서
				uint32_t posCnt = aPos["count"].GetUint(); // 읽을 포지션 개수
				size_t pOffset = GetAccessorDataOffset(bufferViews, aPos); // 포지션 데이터 시작 위치
				size_t pStride = GetAccessorStride(bufferViews, aPos, sizeof(Vector3));

				//***** 바이너리에서 정점 정보 복사
				//*** 이 primitive를 위한 정점 데이터 메모리 할당
				uint32_t svStart = static_cast<uint32_t>(vertices.size());
				vertices.resize(svStart + posCnt);

				for (uint32_t i = 0; i < posCnt; i++)
				{
					Vector3 pos;
					std::memcpy(&pos, cNglTF.m_bin.data() + pOffset + (i * pStride), sizeof(Vector3));
					pos = TRS.TransformPoint(pos);
					// 좌표계 변환
					pos.X = -pos.X;
					vertices[svStart + i].Position = pos;
				}

				//*** Normal
				if (attributes.HasMember("NORMAL"))
				{
					Value& aNormal = accessors[attributes["NORMAL"].GetUint()]; // 노말 엑세서
					uint32_t norCnt = (std::min)(aNormal["count"].GetUint(), posCnt); // 읽을 노말 개수
					size_t nOffset = GetAccessorDataOffset(bufferViews, aNormal); // 노말 데이터 시작 위치
					size_t nStride = GetAccessorStride(bufferViews, aNormal, sizeof(Vector3));

					//* Normal에는 스케일이 포함되면 방향이 바뀌기 때문에 역전체 행렬을 사용해야함
					DirectX::XMMATRIX normalMat = DirectX::XMMatrixTranspose(TRS.Inverse().ToXM());
					for (uint32_t i = 0; i < norCnt; i++)
					{
						Vector3 normal;
						std::memcpy(&normal, cNglTF.m_bin.data() + nOffset + (i * nStride), sizeof(Vector3));
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
					Value& aUv = accessors[attributes["TEXCOORD_0"].GetUint()]; // UV 엑세서
					uint32_t uvCnt = (std::min)(aUv["count"].GetUint(), posCnt); // 읽을 UV 개수
					size_t uvOffset = GetAccessorDataOffset(bufferViews, aUv); // UV 데이터 시작 위치
					size_t uvStride = GetAccessorStride(bufferViews, aUv, sizeof(Vector2));

					for (uint32_t i = 0; i < uvCnt; i++)
					{
						Vector2 uv;
						std::memcpy(&uv, cNglTF.m_bin.data() + uvOffset + (i * uvStride), sizeof(Vector2));
						vertices[svStart + i].UV = uv;
					}
				}

				//*** 탄젠트
				bool hasTangent = attributes.HasMember("TANGENT");
				if (hasTangent)
				{
					Value& aTan = accessors[attributes["TANGENT"].GetUint()];
					uint32_t tanCnt = (std::min)(aTan["count"].GetUint(), posCnt);
					size_t tOffset = GetAccessorDataOffset(bufferViews, aTan);
					size_t tStride = GetAccessorStride(bufferViews, aTan, sizeof(Vector4));
					const DirectX::XMMATRIX M = TRS.ToXM();

					for (uint32_t i = 0; i < tanCnt; i++)
					{
						Vector4 t; // VEC4 float
						std::memcpy(&t, cNglTF.m_bin.data() + tOffset + (i * tStride), sizeof(Vector4));

						// 탄젠트는 표면 방향 → 역전치 X, TRS 그대로 w=0
						Vector3 tv = Vector3::FromXM(DirectX::XMVector3Normalize(
							DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(t.X, t.Y, t.Z, 0.0f), M)));
						tv.X = -tv.X;                              // 좌표계 미러링
						const float w = (det > 0.0f) ? -t.W : t.W; // 손 방향이 뒤집히면 부호 반전
						vertices[svStart + i].Tangent = { tv.X, tv.Y, tv.Z, w };
					}
				}

				//***** 인덱스 복사
				Value& aIndex = accessors[primitive["indices"].GetUint()];
				size_t iOffset = GetAccessorDataOffset(bufferViews, aIndex);
				uint32_t indexCnt = aIndex["count"].GetUint();
				const uint32_t componentType = aIndex["componentType"].GetUint(); // 5121: u8, 5123: u16, 5125: u32
				const uint32_t iStart = static_cast<uint32_t>(indices.size());  // = sm.startIndex
				indices.resize(iStart + indexCnt);
				const uint8_t* indexData = cNglTF.m_bin.data() + iOffset;
				for (uint32_t i = 0; i < indexCnt; i++)
				{
					if (componentType == 5125)
					{
						uint32_t vi;
						std::memcpy(&vi, indexData + (i * sizeof(uint32_t)), sizeof(uint32_t));
						indices[iStart + i] = vi;
					}
					else if (componentType == 5123)
					{
						uint16_t vi;
						std::memcpy(&vi, indexData + (i * sizeof(uint16_t)), sizeof(uint16_t));
						indices[iStart + i] = vi;
					}
					else
					{
						indices[iStart + i] = indexData[i];
					}
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

	std::unique_ptr<MeshAsset> CNglTFImporter::LoadSkinnedMeshAsset(CNglTF& cNglTF, uint32_t skinIndex)
	{
		Value& doc = cNglTF.m_doc;
		if (!doc.HasMember("skins") || skinIndex >= doc["skins"].Size())
			return nullptr;

		Value& nodes = doc["nodes"];

		std::vector<MaterialData> materialSlots;
		LoadMaterials(cNglTF, materialSlots);

		Value& accessors = doc["accessors"];
		Value& bufferViews = doc["bufferViews"];
		const uint8_t* bin = cNglTF.m_bin.data();

		std::vector<SkinnedVertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<SubMesh> subMeshes;

		for (Value& meshNode : nodes.GetArray())
		{
			if (!meshNode.HasMember("skin") || !meshNode.HasMember("mesh") || meshNode["skin"].GetUint() != skinIndex)
				continue;

			Value& mesh = doc["meshes"][meshNode["mesh"].GetUint()];
			for (Value& primitive : mesh["primitives"].GetArray())
			{
				Value& attributes = primitive["attributes"];

				SubMesh sm;
				sm.startIndex = static_cast<uint32_t>(indices.size());
				sm.baseVertex = static_cast<uint32_t>(vertices.size());
				if (primitive.HasMember("material"))
				{
					sm.materialSlot = primitive["material"].GetUint();
				}

				Value& aPos = accessors[attributes["POSITION"].GetUint()];
				Value& bPos = bufferViews[aPos["bufferView"].GetUint()];
				const uint32_t vertexCount = aPos["count"].GetUint();
				const size_t pOffset = (bPos.HasMember("byteOffset") ? bPos["byteOffset"].GetUint() : 0u) + (aPos.HasMember("byteOffset") ? aPos["byteOffset"].GetUint() : 0u);

				const uint32_t vStart = static_cast<uint32_t>(vertices.size());
				vertices.resize(vStart + vertexCount);
				for (uint32_t i = 0; i < vertexCount; i++)
				{
					Vector3 pos;
					std::memcpy(&pos, bin + pOffset + i * sizeof(Vector3), sizeof(Vector3));
					pos.X = -pos.X;
					vertices[vStart + i].Position = pos;
				}

				if (attributes.HasMember("NORMAL"))
				{
					Value& aNormal = accessors[attributes["NORMAL"].GetUint()];
					Value& bNormal = bufferViews[aNormal["bufferView"].GetUint()];
					const size_t nOffset = (bNormal.HasMember("byteOffset") ? bNormal["byteOffset"].GetUint() : 0u) + (aNormal.HasMember("byteOffset") ? aNormal["byteOffset"].GetUint() : 0u);
					for (uint32_t i = 0; i < vertexCount; i++)
					{
						Vector3 normal;
						std::memcpy(&normal, bin + nOffset + i * sizeof(Vector3), sizeof(Vector3));
						normal.X = -normal.X;
						vertices[vStart + i].Normal = normal;
					}
				}

				if (attributes.HasMember("TEXCOORD_0"))
				{
					Value& aUv = accessors[attributes["TEXCOORD_0"].GetUint()];
					Value& bUv = bufferViews[aUv["bufferView"].GetUint()];
					const size_t uvOffset = (bUv.HasMember("byteOffset") ? bUv["byteOffset"].GetUint() : 0u) + (aUv.HasMember("byteOffset") ? aUv["byteOffset"].GetUint() : 0u);
					for (uint32_t i = 0; i < vertexCount; i++)
					{
						std::memcpy(&vertices[vStart + i].UV, bin + uvOffset + i * sizeof(Vector2), sizeof(Vector2));
					}
				}

				if (attributes.HasMember("JOINTS_0"))
				{
					Value& aJoint = accessors[attributes["JOINTS_0"].GetUint()];
					Value& bJoint = bufferViews[aJoint["bufferView"].GetUint()];
					const size_t jOffset = (bJoint.HasMember("byteOffset") ? bJoint["byteOffset"].GetUint() : 0u) + (aJoint.HasMember("byteOffset") ? aJoint["byteOffset"].GetUint() : 0u);
					const bool isByte = aJoint["componentType"].GetUint() == 5121;
					for (uint32_t i = 0; i < vertexCount; i++)
					{
						for (uint32_t k = 0; k < 4; k++)
						{
							if (isByte)
							{
								vertices[vStart + i].Joints[k] = bin[jOffset + i * 4 + k];
							}
							else
							{
								uint16_t joint;
								std::memcpy(&joint, bin + jOffset + (i * 4 + k) * sizeof(uint16_t), sizeof(uint16_t));
								vertices[vStart + i].Joints[k] = joint;
							}
						}
					}
				}

				if (attributes.HasMember("WEIGHTS_0"))
				{
					Value& aWeight = accessors[attributes["WEIGHTS_0"].GetUint()];
					Value& bWeight = bufferViews[aWeight["bufferView"].GetUint()];
					const size_t wOffset = (bWeight.HasMember("byteOffset") ? bWeight["byteOffset"].GetUint() : 0u) + (aWeight.HasMember("byteOffset") ? aWeight["byteOffset"].GetUint() : 0u);
					for (uint32_t i = 0; i < vertexCount; i++)
					{
						std::memcpy(vertices[vStart + i].Weights, bin + wOffset + i * sizeof(float) * 4, sizeof(float) * 4);
					}
				}

				const bool hasTangent = attributes.HasMember("TANGENT");
				if (hasTangent)
				{
					Value& aTangent = accessors[attributes["TANGENT"].GetUint()];
					Value& bTangent = bufferViews[aTangent["bufferView"].GetUint()];
					const size_t tOffset = (bTangent.HasMember("byteOffset") ? bTangent["byteOffset"].GetUint() : 0u) + (aTangent.HasMember("byteOffset") ? aTangent["byteOffset"].GetUint() : 0u);
					for (uint32_t i = 0; i < vertexCount; i++)
					{
						Vector4 t;
						std::memcpy(&t, bin + tOffset + i * sizeof(Vector4), sizeof(Vector4));
						const Vector3 tv = Vector3(-t.X, t.Y, t.Z).Normalized();
						vertices[vStart + i].Tangent = { tv.X, tv.Y, tv.Z, -t.W };
					}
				}

				Value& aIndex = accessors[primitive["indices"].GetUint()];
				Value& bIndex = bufferViews[aIndex["bufferView"].GetUint()];
				const size_t iOffset = (bIndex.HasMember("byteOffset") ? bIndex["byteOffset"].GetUint() : 0u) + (aIndex.HasMember("byteOffset") ? aIndex["byteOffset"].GetUint() : 0u);
				const uint32_t indexCount = aIndex["count"].GetUint();
				const bool isShort = aIndex["componentType"].GetUint() == 5123;
				const uint32_t iStart = static_cast<uint32_t>(indices.size());
				indices.resize(iStart + indexCount);
				for (uint32_t i = 0; i < indexCount; i++)
				{
					if (isShort)
					{
						uint16_t index;
						std::memcpy(&index, bin + iOffset + i * sizeof(uint16_t), sizeof(uint16_t));
						indices[iStart + i] = index;
					}
					else
					{
						std::memcpy(&indices[iStart + i], bin + iOffset + i * sizeof(uint32_t), sizeof(uint32_t));
					}
				}
				for (uint32_t i = 0; i + 2 < indexCount; i += 3)
				{
					std::swap(indices[iStart + i + 1], indices[iStart + i + 2]);
				}

				if (!hasTangent)
				{
					std::vector<Vector3> tangents(vertexCount);
					std::vector<Vector3> bitangents(vertexCount);
					for (uint32_t i = 0; i + 2 < indexCount; i += 3)
					{
						const uint32_t i0 = indices[iStart + i];
						const uint32_t i1 = indices[iStart + i + 1];
						const uint32_t i2 = indices[iStart + i + 2];
						if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
							continue;

						const SkinnedVertex& v0 = vertices[vStart + i0];
						const SkinnedVertex& v1 = vertices[vStart + i1];
						const SkinnedVertex& v2 = vertices[vStart + i2];

						const Vector3 e1 = v1.Position - v0.Position;
						const Vector3 e2 = v2.Position - v0.Position;
						const float du1 = v1.UV.X - v0.UV.X, dv1 = v1.UV.Y - v0.UV.Y;
						const float du2 = v2.UV.X - v0.UV.X, dv2 = v2.UV.Y - v0.UV.Y;
						const float uvDet = du1 * dv2 - du2 * dv1;
						if (std::abs(uvDet) < 1e-12f)
							continue;

						const float r = 1.0f / uvDet;
						const Vector3 t = (e1 * dv2 - e2 * dv1) * r;
						const Vector3 b = (e2 * du1 - e1 * du2) * r;
						for (uint32_t local : { i0, i1, i2 })
						{
							tangents[local] += t;
							bitangents[local] += b;
						}
					}

					for (uint32_t local = 0; local < vertexCount; ++local)
					{
						SkinnedVertex& vertex = vertices[vStart + local];
						const Vector3& n = vertex.Normal;
						const Vector3& accumulated = tangents[local];
						Vector3 t = (accumulated - n * Vector3::Dot(n, accumulated)).Normalized();
						if (t.IsNearlyZero())
							t = AnyPerpendicular(n);
						const float w = Vector3::Dot(Vector3::Cross(n, t), bitangents[local]) < 0.0f ? -1.0f : 1.0f;
						vertex.Tangent = { t.X, t.Y, t.Z, w };
					}
				}

				sm.indexCount = indexCount;
				subMeshes.push_back(sm);
			}
		}
		if (subMeshes.empty())
			return nullptr;

		Value& skin = doc["skins"][skinIndex];
		Value& joints = skin["joints"];

		std::vector<std::string> nodePaths(nodes.Size());
		const uint32_t sceneIndex = doc.HasMember("scene") ? doc["scene"].GetUint() : 0u;
		for (Value& rootNode : doc["scenes"][sceneIndex]["nodes"].GetArray())
		{
			BuildPaths(cNglTF, rootNode.GetUint(), "", nodePaths);
		}

		std::vector<int32_t> parents(nodes.Size(), -1);
		for (uint32_t i = 0; i < nodes.Size(); i++)
		{
			if (!nodes[i].HasMember("children"))
				continue;
			for (Value& child : nodes[i]["children"].GetArray())
			{
				parents[child.GetUint()] = static_cast<int32_t>(i);
			}
		}

		std::vector<bool> isJoint(nodes.Size(), false);
		for (Value& joint : joints.GetArray())
		{
			isJoint[joint.GetUint()] = true;
		}

		int32_t rootJoint = joints.Size() > 0 ? static_cast<int32_t>(joints[0].GetUint()) : -1;
		while (rootJoint >= 0 && parents[rootJoint] >= 0 && isJoint[parents[rootJoint]])
		{
			rootJoint = parents[rootJoint];
		}
		const int32_t skeletonNode = rootJoint >= 0 ? parents[rootJoint] : -1;
		const std::string skeletonPrefix = skeletonNode >= 0 ? nodePaths[skeletonNode] + "/" : "";

		std::vector<std::string> bonePaths;
		for (Value& joint : joints.GetArray())
		{
			const std::string& path = nodePaths[joint.GetUint()];
			bonePaths.push_back(path.starts_with(skeletonPrefix) ? path.substr(skeletonPrefix.size()) : path);
		}

		std::vector<Matrix4> inverseBindMatrices(joints.Size(), Matrix4::Identity);
		if (skin.HasMember("inverseBindMatrices"))
		{
			Value& aIbm = accessors[skin["inverseBindMatrices"].GetUint()];
			Value& bIbm = bufferViews[aIbm["bufferView"].GetUint()];
			const size_t mOffset = (bIbm.HasMember("byteOffset") ? bIbm["byteOffset"].GetUint() : 0u) + (aIbm.HasMember("byteOffset") ? aIbm["byteOffset"].GetUint() : 0u);
			for (uint32_t j = 0; j < joints.Size(); j++)
			{
				Matrix4& ibm = inverseBindMatrices[j];
				std::memcpy(ibm.M, bin + mOffset + j * sizeof(float) * 16, sizeof(float) * 16);
				for (uint32_t row = 0; row < 4; row++)
				{
					for (uint32_t col = 0; col < 4; col++)
					{
						if ((row == 0) != (col == 0))
						{
							ibm.M[row][col] = -ibm.M[row][col];
						}
					}
				}
			}
		}

		return std::make_unique<MeshAsset>(std::move(vertices), std::move(indices), std::move(subMeshes),
			std::move(inverseBindMatrices), std::move(bonePaths), std::move(materialSlots));
	}

	GameObject* CNglTFImporter::LoadSkeleton(CNglTF& cNglTF, Scene& scene, const std::string& name, std::vector<GameObject*>& nodeObjects)
	{
		Value& doc = cNglTF.m_doc;
		GameObject* top = scene.GetGameObject(scene.AddGameObject(name));

		Value& nodes = doc["nodes"];
		nodeObjects.assign(nodes.Size(), nullptr);
		for (uint32_t i = 0; i < nodes.Size(); i++)
		{
			Value& node = nodes[i];
			if (node.HasMember("skin") && node.HasMember("mesh"))
				continue;

			const std::string nodeName = node.HasMember("name") ? node["name"].GetString() : "Node_" + std::to_string(i);
			nodeObjects[i] = scene.GetGameObject(scene.AddGameObject(nodeName));

			TransformComponent* transform = nodeObjects[i]->GetTransform();
			if (node.HasMember("translation"))
			{
				Value& t = node["translation"];
				transform->SetPosition({ -t[0].GetFloat(), t[1].GetFloat(), t[2].GetFloat() });
			}
			if (node.HasMember("rotation"))
			{
				Value& r = node["rotation"];
				transform->SetRotation({ r[0].GetFloat(), -r[1].GetFloat(), -r[2].GetFloat(), r[3].GetFloat() });
			}
			if (node.HasMember("scale"))
			{
				Value& s = node["scale"];
				transform->SetScale({ s[0].GetFloat(), s[1].GetFloat(), s[2].GetFloat() });
			}
		}

		const uint32_t sceneIndex = doc.HasMember("scene") ? doc["scene"].GetUint() : 0u;
		for (Value& rootNode : doc["scenes"][sceneIndex]["nodes"].GetArray())
		{
			if (nodeObjects[rootNode.GetUint()])
				nodeObjects[rootNode.GetUint()]->GetTransform()->SetParent(top->GetTransform());
		}
		for (uint32_t i = 0; i < nodes.Size(); i++)
		{
			if (!nodeObjects[i] || !nodes[i].HasMember("children"))
				continue;
			for (Value& child : nodes[i]["children"].GetArray())
			{
				if (nodeObjects[child.GetUint()])
					nodeObjects[child.GetUint()]->GetTransform()->SetParent(nodeObjects[i]->GetTransform());
			}
		}

		top->AddComponent<AnimatorComponent>();
		return top;
	}

	bool CNglTFImporter::ImportSkeleton(const std::string& gltfPath, const std::string& targetDir, const std::string& clipPath)
	{
		CNglTF cNglTF;
		if (!ReadglTF(gltfPath, cNglTF))
			return false;

		const std::string modelName = ToUtf8(ToPath(gltfPath).stem().u8string());
		Scene scene(modelName);
		std::vector<GameObject*> nodeObjects;
		GameObject* top = LoadSkeleton(cNglTF, scene, modelName, nodeObjects);
		if (!clipPath.empty())
		{
			top->GetComponent<AnimatorComponent>()->SetClip(clipPath);
		}

		return SceneSerializer::SavePrefab(*top, ToUtf8((ToPath(targetDir) / (modelName + "_Skeleton.prefab")).generic_u8string()));
	}

	bool CNglTFImporter::ImportSkeletonModel(const std::string& gltfPath, const std::string& targetDir, const std::string& clipPath)
	{
		const std::filesystem::path source = ToPath(gltfPath);
		const std::filesystem::path target = ToPath(targetDir);
		const std::filesystem::path copiedGltf = target / source.filename();

		CNglTF sourceGltf;
		if (!ReadglTF(gltfPath, sourceGltf))
			return false;

		std::vector<std::filesystem::path> files = { source.filename() };
		for (Value& buffer : sourceGltf.m_doc["buffers"].GetArray())
		{
			if (buffer.HasMember("uri"))
				files.push_back(ToPath(buffer["uri"].GetString()));
		}
		if (sourceGltf.m_doc.HasMember("images"))
		{
			for (Value& image : sourceGltf.m_doc["images"].GetArray())
			{
				if (image.HasMember("uri"))
					files.push_back(ToPath(image["uri"].GetString()));
			}
		}

		std::error_code error;
		std::filesystem::create_directories(target, error);
		for (const std::filesystem::path& file : files)
		{
			const std::filesystem::path from = source.parent_path() / file;
			const std::filesystem::path to = target / file;
			if (std::filesystem::equivalent(from, to, error))
				continue;
			std::filesystem::create_directories(to.parent_path(), error);
			std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, error);
		}

		const std::string copiedPath = ToUtf8(copiedGltf.generic_u8string());
		CNglTF cNglTF;
		if (!ReadglTF(copiedPath, cNglTF))
			return false;

		Value& doc = cNglTF.m_doc;
		const std::string assetPath = AssetManager::ToAssetPath(copiedPath);
		const std::string modelName = ToUtf8(source.stem().u8string());

		Scene scene(modelName);
		std::vector<GameObject*> nodeObjects;
		GameObject* top = LoadSkeleton(cNglTF, scene, modelName, nodeObjects);
		if (!clipPath.empty())
		{
			top->GetComponent<AnimatorComponent>()->SetClip(clipPath);
		}

		const uint32_t skinCount = doc.HasMember("skins") ? doc["skins"].Size() : 0u;
		for (uint32_t i = 0; i < skinCount; i++)
		{
			Value& skin = doc["skins"][i];
			const std::string skinName = skin.HasMember("name") ? skin["name"].GetString() : "Skin_" + std::to_string(i);

			std::vector<TransformComponent*> jointTransforms;
			for (Value& joint : skin["joints"].GetArray())
			{
				if (nodeObjects[joint.GetUint()])
					jointTransforms.push_back(nodeObjects[joint.GetUint()]->GetTransform());
			}
			TransformComponent* skeletonRoot = top->GetTransform();
			if (!jointTransforms.empty())
			{
				TransformComponent* rootJoint = jointTransforms[0];
				while (rootJoint->GetParent() && std::find(jointTransforms.begin(), jointTransforms.end(), rootJoint->GetParent()) != jointTransforms.end())
				{
					rootJoint = rootJoint->GetParent();
				}
				if (rootJoint->GetParent())
					skeletonRoot = rootJoint->GetParent();
			}
			GameObject* meshObject = scene.GetGameObject(scene.AddGameObject(skinName));
			meshObject->GetTransform()->SetParent(top->GetTransform());

			SkinnedMeshRendererComponent* renderer = meshObject->AddComponent<SkinnedMeshRendererComponent>();
			renderer->SetMesh(assetPath + "#skin" + std::to_string(i));
			renderer->SetSkeleton(skeletonRoot);
		}

		return SceneSerializer::SavePrefab(*top, ToUtf8((target / (modelName + "_SkeletonModel.prefab")).generic_u8string()));
	}


}


