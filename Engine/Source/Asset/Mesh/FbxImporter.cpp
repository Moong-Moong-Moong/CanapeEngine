#include "Asset/Mesh/FbxImporter.h"

#include "ThirdParty/ufbx/ufbx.h"

#include <vector>
#include <cmath>
#include <unordered_map>

namespace Canape
{
	namespace
	{
		struct SceneDeleter
		{
			void operator()(ufbx_scene* scene) const { ufbx_free_scene(scene); }
		};

		using ScenePtr = std::unique_ptr<ufbx_scene, SceneDeleter>;

		Vector3 ToVector3(const ufbx_vec3& v)
		{
			return { static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) };
		}

		void SetError(std::string* outError, std::string message)
		{
			if (outError)
			{
				*outError = std::move(message);
			}
		}

		// ===== [Tangent] 헬퍼 시작 =====
		// 위치/노말/UV 인덱스가 모두 같은 코너는 같은 논리 정점으로 보고 탄젠트를 누적
		// 하드 엣지(노말 인덱스 다름)와 UV 이음새(UV 인덱스 다름)는 자동으로 분리된다.
		struct CornerKey
		{
			uint32_t Position;
			uint32_t Normal;
			uint32_t UV;
			bool operator==(const CornerKey&) const = default;
		};

		struct CornerKeyHash
		{
			size_t operator()(const CornerKey& key) const
			{
				uint64_t hash = key.Position;
				hash = hash * 0x9E3779B97F4A7C15ull ^ key.Normal;
				hash = hash * 0x9E3779B97F4A7C15ull ^ key.UV;
				return static_cast<size_t>(hash);
			}
		};

		// UV가 없거나 퇴화해서 탄젠트를 구할 수 없을 때 쓰는 N에 수직인 임의 벡터.
		Vector3 AnyPerpendicular(const Vector3& normal)
		{
			const Vector3 axis = std::abs(normal.Y) < 0.99f ? Vector3::Up : Vector3::Right;
			return Vector3::Cross(axis, normal).Normalized();
		}

		// vertices는 연속된 삼각형 리스트, groups[i]는 i번 정점이 속한 논리 정점 번호.
		// 최종 Position/Normal/UV 기준으로 계산하므로 미러/음수 스케일도 w에 반영된다.
		// 셰이더에서는 B = cross(N, T.xyz) * T.w 로 같은 순서를 써야 한다.
		void ComputeTangents(StaticVertex* vertices, size_t count, const uint32_t* groups, size_t groupCount)
		{
			std::vector<Vector3> tangents(groupCount);
			std::vector<Vector3> bitangents(groupCount);
			for (size_t i = 0; i + 2 < count; i += 3)
			{
				const StaticVertex& v0 = vertices[i];
				const StaticVertex& v1 = vertices[i + 1];
				const StaticVertex& v2 = vertices[i + 2];
				const Vector3 e1 = v1.Position - v0.Position;
				const Vector3 e2 = v2.Position - v0.Position;
				const float du1 = v1.UV.X - v0.UV.X, dv1 = v1.UV.Y - v0.UV.Y;
				const float du2 = v2.UV.X - v0.UV.X, dv2 = v2.UV.Y - v0.UV.Y;
				const float det = du1 * dv2 - du2 * dv1;
				if (std::abs(det) < 1e-12f)
					continue; // UV가 퇴화한 삼각형은 기여하지 않음
				const float r = 1.0f / det;
				const Vector3 t = (e1 * dv2 - e2 * dv1) * r;
				const Vector3 b = (e2 * du1 - e1 * du2) * r;
				for (size_t c = 0; c < 3; ++c)
				{
					tangents[groups[i + c]] += t;
					bitangents[groups[i + c]] += b;
				}
			}

			for (size_t i = 0; i < count; ++i)
			{
				StaticVertex& vertex = vertices[i];
				const Vector3& n = vertex.Normal;
				const Vector3& accumulated = tangents[groups[i]];
				Vector3 t = (accumulated - n * Vector3::Dot(n, accumulated)).Normalized(); // Gram-Schmidt
				if (t.IsNearlyZero())
					t = AnyPerpendicular(n); // 노말이 0이면 탄젠트도 0으로 남음
				const float w = Vector3::Dot(Vector3::Cross(n, t), bitangents[groups[i]]) < 0.0f ? -1.0f : 1.0f;
				vertex.Tangent = { t.X, t.Y, t.Z, w };
			}
		}
		// ===== [Tangent] 헬퍼 끝 =====
	}

	std::unique_ptr<MeshAsset> FbxImporter::LoadMesh(const std::string& path, std::string* outError)
	{
		ufbx_load_opts options{};
		options.target_axes = { UFBX_COORDINATE_AXIS_NEGATIVE_X, UFBX_COORDINATE_AXIS_POSITIVE_Y, UFBX_COORDINATE_AXIS_POSITIVE_Z };
		options.target_unit_meters = 1.0f;
		options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
		options.handedness_conversion_axis = UFBX_MIRROR_AXIS_X;
		options.generate_missing_normals = true;

		ufbx_error error{};
		ScenePtr scene(ufbx_load_file(path.c_str(), &options, &error));
		if (!scene)
		{
			SetError(outError, path + ": " + std::string(error.description.data, error.description.length));
			return nullptr;
		}

		std::vector<StaticVertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<SubMesh> submeshes;
		std::vector<uint32_t> triangleCorners;
		// ===== [Tangent] 정점 그룹 버퍼 =====
		std::vector<uint32_t> tangentGroups;
		std::unordered_map<CornerKey, uint32_t, CornerKeyHash> tangentGroupOf;
		// ===== [Tangent] 정점 그룹 버퍼 끝 =====
		if (scene->materials.count > UINT32_MAX)
		{
			SetError(outError, path + ": too many material slots");
			return nullptr;
		}

		for (const ufbx_node* node : scene->nodes)
		{
			const ufbx_mesh* mesh = node->mesh;
			if (!mesh)
			{
				continue;
			}

			triangleCorners.resize(mesh->max_face_triangles * 3);
			const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&node->geometry_to_world);

			// ===== [Tangent] 노드 단위 초기화 (ufbx 인덱스는 메시마다 따로 매겨짐) =====
			const size_t nodeVertexStart = vertices.size();
			tangentGroups.clear();
			tangentGroupOf.clear();
			// ===== [Tangent] 노드 단위 초기화 끝 =====

			for (const ufbx_mesh_part& part : mesh->material_parts)
			{
				const ufbx_material* material = part.index < node->materials.count ? node->materials[part.index] : nullptr;
				const uint32_t materialSlot = material ? material->typed_id : static_cast<uint32_t>(scene->materials.count);
				const uint32_t startIndex = static_cast<uint32_t>(indices.size());
				for (uint32_t faceIndex : part.face_indices)
				{
					const uint32_t triangleCount = ufbx_triangulate_face(triangleCorners.data(), triangleCorners.size(), mesh, mesh->faces[faceIndex]);
					if (uint64_t(vertices.size()) + uint64_t(triangleCount) * 3 > UINT32_MAX)
					{
						SetError(outError, path + ": too many vertices");
						return nullptr;
					}
					for (uint32_t i = 0; i < triangleCount * 3; ++i)
					{
						const uint32_t corner = triangleCorners[i];
						StaticVertex vertex;
						vertex.Position = ToVector3(ufbx_transform_position(&node->geometry_to_world,
							ufbx_get_vertex_vec3(&mesh->vertex_position, corner)));
						if (mesh->vertex_normal.exists)
						{
							vertex.Normal = ToVector3(ufbx_vec3_normalize(ufbx_transform_direction(&normalMatrix,
								ufbx_get_vertex_vec3(&mesh->vertex_normal, corner))));
							if (!std::isfinite(vertex.Normal.X) || !std::isfinite(vertex.Normal.Y) || !std::isfinite(vertex.Normal.Z))
								vertex.Normal = {};
						}
						if (mesh->vertex_uv.exists)
						{
							const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, corner);
							vertex.UV = { static_cast<float>(uv.x), static_cast<float>(uv.y) };
						}
						indices.push_back(static_cast<uint32_t>(vertices.size()));
						vertices.push_back(vertex);

						// ===== [Tangent] 코너를 논리 정점 그룹에 등록 =====
						const CornerKey key{
							mesh->vertex_position.indices.data[corner],
							mesh->vertex_normal.exists ? mesh->vertex_normal.indices.data[corner] : 0u,
							mesh->vertex_uv.exists ? mesh->vertex_uv.indices.data[corner] : 0u };
						tangentGroups.push_back(tangentGroupOf.try_emplace(key, static_cast<uint32_t>(tangentGroupOf.size())).first->second);
						// ===== [Tangent] 코너 등록 끝 =====
					}
				}
				const uint32_t indexCount = static_cast<uint32_t>(indices.size()) - startIndex;
				if (indexCount != 0)
					submeshes.push_back({ startIndex, indexCount, 0, materialSlot });
			}

			// ===== [Tangent] 이 노드의 정점 탄젠트 계산 =====
			ComputeTangents(vertices.data() + nodeVertexStart, vertices.size() - nodeVertexStart,
				tangentGroups.data(), tangentGroupOf.size());
			// ===== [Tangent] 탄젠트 계산 끝 =====
		}

		if (vertices.empty())
		{
			SetError(outError, path + ": no mesh data");
			return nullptr;
		}

		if (const char* validationError = ValidateMeshIndices(vertices.size(), indices, submeshes))
		{
			SetError(outError, path + ": " + validationError);
			return nullptr;
		}
		if (outError) outError->clear();
		return std::make_unique<MeshAsset>(std::move(vertices), std::move(indices), std::move(submeshes));
	}
}
