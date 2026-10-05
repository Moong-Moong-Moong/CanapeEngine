#include "Asset/FbxImporter.h"

#include "ThirdParty/ufbx/ufbx.h"

#include <vector>

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

		ufbx_error error{};
		ScenePtr scene(ufbx_load_file(path.c_str(), &options, &error));
		if (!scene)
		{
			SetError(outError, path + ": " + std::string(error.description.data, error.description.length));
			return nullptr;
		}

		std::vector<Vector3> positions;
		std::vector<uint32_t> indices;
		std::vector<uint32_t> triangleCorners;

		for (const ufbx_node* node : scene->nodes)
		{
			const ufbx_mesh* mesh = node->mesh;
			if (!mesh)
			{
				continue;
			}

			triangleCorners.resize(mesh->max_face_triangles * 3);

			for (const ufbx_face& face : mesh->faces)
			{
				const uint32_t triangleCount = ufbx_triangulate_face(triangleCorners.data(), triangleCorners.size(), mesh, face);

				for (uint32_t i = 0; i < triangleCount * 3; ++i)
				{
					const ufbx_vec3 local = ufbx_get_vertex_vec3(&mesh->vertex_position, triangleCorners[i]);
					const ufbx_vec3 world = ufbx_transform_position(&node->geometry_to_world, local);

					indices.push_back(static_cast<uint32_t>(positions.size()));
					positions.push_back(ToVector3(world));
				}
			}
		}

		if (positions.empty())
		{
			SetError(outError, path + ": no mesh data");
			return nullptr;
		}

		return std::make_shared<Mesh>(std::move(positions), std::move(indices));
	}
}
