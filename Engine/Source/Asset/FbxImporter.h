#pragma once

#include "Renderer/Mesh.h"

#include <memory>
#include <string>

namespace Canape
{
	class FbxImporter
	{
	public:
		static std::shared_ptr<Mesh> LoadMesh(const std::string& path, std::string* outError = nullptr);
	};
}
