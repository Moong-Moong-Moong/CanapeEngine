#pragma once

#include "Renderer/Mesh.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Canape
{
	class ResourceManager
	{
	public:
		std::shared_ptr<Mesh> LoadMesh(const std::string& path);
		void Clear();

	private:
		std::unordered_map<std::string, std::shared_ptr<Mesh>> m_Meshes;
	};
}
