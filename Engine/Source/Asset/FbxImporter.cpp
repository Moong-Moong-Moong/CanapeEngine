#include "Asset/FbxImporter.h"

#include "ThirdParty/ufbx/ufbx.h"

#include <vector>
#include <cmath>

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
	}

	std::shared_ptr<Mesh> FbxImporter::LoadMesh(const std::string& path, std::string* outError)
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

			for (const ufbx_mesh_part& part : mesh->material_parts)
			{
				// Resolve the node's actual binding before flattening. A file-material index
				// is a Mesh-local slot here; identical FBX materials share the same slot.
				// The slot after the file's materials is reserved for unassigned/default material.
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
					}
				}
				const uint32_t indexCount = static_cast<uint32_t>(indices.size()) - startIndex;
				if (indexCount != 0)
					submeshes.push_back({ startIndex, indexCount, 0, materialSlot });
			}
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
		return std::make_shared<Mesh>(std::move(vertices), std::move(indices), std::move(submeshes));
	}
}
