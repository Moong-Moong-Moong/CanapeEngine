#pragma once

#include "Asset/Mesh/MeshAsset.h"

#include <memory>
#include <string>

namespace Canape
{
	class FbxImporter
	{
	public:
		static std::unique_ptr<MeshAsset> LoadMesh(const std::string& path, std::string* outError = nullptr);
	};
}
