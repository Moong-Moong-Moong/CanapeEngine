#include "Asset/Mesh/PrimitiveMeshes.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Canape
{
	namespace
	{
		constexpr float HalfExtent = 0.5f;

		// Seen from outside, a face is clockwise in a left-handed system; right = normal x up.
		void AppendFace(PrimitiveMeshData& mesh, const Vector3& center, const Vector3& normal, const Vector3& up)
		{
			const Vector3 right = Vector3::Cross(normal, up);
			const Vector3 r = right * HalfExtent;
			const Vector3 u = up * HalfExtent;
			const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

			mesh.vertices.push_back({ center - r + u, normal, { 0.0f, 0.0f } });
			mesh.vertices.push_back({ center + r + u, normal, { 1.0f, 0.0f } });
			mesh.vertices.push_back({ center + r - u, normal, { 1.0f, 1.0f } });
			mesh.vertices.push_back({ center - r - u, normal, { 0.0f, 1.0f } });

			const uint32_t quad[] = { 0, 1, 2, 0, 2, 3 };
			for (const uint32_t corner : quad)
			{
				mesh.indices.push_back(base + corner);
			}
		}
	}

	PrimitiveMeshData PrimitiveMeshes::BuildCube()
	{
		struct Face
		{
			Vector3 normal;
			Vector3 up;
		};
		const Face faces[] =
		{
			{ {  1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f } },
			{ { -1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f } },
			{ {  0.0f,  1.0f,  0.0f }, { 0.0f, 0.0f,  1.0f } },
			{ {  0.0f, -1.0f,  0.0f }, { 0.0f, 0.0f, -1.0f } },
			{ {  0.0f,  0.0f,  1.0f }, { 0.0f, 1.0f,  0.0f } },
			{ {  0.0f,  0.0f, -1.0f }, { 0.0f, 1.0f,  0.0f } },
		};

		PrimitiveMeshData mesh;
		mesh.vertices.reserve(24);
		mesh.indices.reserve(36);

		for (const Face& face : faces)
		{
			AppendFace(
                mesh,
                face.normal * HalfExtent,
                face.normal,
                face.up
            );
		}
		return mesh;
	}

	PrimitiveMeshData PrimitiveMeshes::BuildPlane()
	{
		PrimitiveMeshData mesh;
		mesh.vertices.reserve(4);
		mesh.indices.reserve(6);

		AppendFace(
            mesh,
            {},
            { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f }
        );
		return mesh;
	}

	PrimitiveMeshData PrimitiveMeshes::BuildSphere(uint32_t segments, uint32_t rings)
	{
		segments = std::max(segments, 3u);
		rings = std::max(rings, 2u);

		PrimitiveMeshData mesh;
		mesh.vertices.reserve(static_cast<size_t>(segments + 1) * (rings + 1));
		mesh.indices.reserve(static_cast<size_t>(segments) * rings * 6);

		for (uint32_t ring = 0; ring <= rings; ++ring)
		{
			const float v = static_cast<float>(ring) / static_cast<float>(rings);
			const float theta = std::numbers::pi_v<float> * v;
			const float y = std::cos(theta);
			const float radial = std::sin(theta);
			for (uint32_t segment = 0; segment <= segments; ++segment)
			{
				const float u = static_cast<float>(segment) / static_cast<float>(segments);
				const float phi = 2.0f * std::numbers::pi_v<float> * u;
				const Vector3 normal{ radial * std::cos(phi), y, radial * std::sin(phi) };

				mesh.vertices.push_back({
                    normal * HalfExtent,
                    normal,
                    { u, v }
                });
			}
		}

		const uint32_t stride = segments + 1;
		for (uint32_t ring = 0; ring < rings; ++ring)
		{
			for (uint32_t segment = 0; segment < segments; ++segment)
			{
				const uint32_t a = ring * stride + segment;
				const uint32_t b = a + 1;
				const uint32_t c = a + stride;
				const uint32_t d = c + 1;
				// The pole rows collapse to a point, so one triangle of each pole quad is degenerate.
				if (ring != 0)
				{
					mesh.indices.insert(mesh.indices.end(), { a, b, c });
				}
				if (ring != rings - 1)
				{
					mesh.indices.insert(mesh.indices.end(), { b, d, c });
				}
			}
		}
		return mesh;
	}
}
