#pragma once

#include "DX11Renderer/Data/MeshData.h"

#include <cstdint>
#include <vector>

namespace Canape
{
	struct PrimitiveMeshData
	{
		std::vector<StaticVertex> vertices;
		std::vector<uint32_t> indices;
	};

	// Canape conventions: left-handed, +Y up, front faces wind clockwise seen from outside,
	// normals point outward, one submesh in material slot 0 (the MeshAsset default).
	class PrimitiveMeshes
	{
	public:
		// Unit cube, half extent 0.5, flat normals, UVs 0..1 per face.
		static PrimitiveMeshData BuildCube();

		// UV sphere of radius 0.5 centred on the origin, poles on +Y/-Y.
		static PrimitiveMeshData BuildSphere(uint32_t segments = 32, uint32_t rings = 16);

		// 1x1 quad in the XZ plane at y = 0, facing +Y.
		static PrimitiveMeshData BuildPlane();

		// Cylinder of radius 0.5 and height 1 centred on the origin, axis +Y; separate cap and side vertices.
		static PrimitiveMeshData BuildCylinder(uint32_t segments = 32);
	};
}
