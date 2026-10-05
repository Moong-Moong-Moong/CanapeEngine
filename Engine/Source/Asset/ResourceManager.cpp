#include "Asset/ResourceManager.h"
#include "Asset/FbxImporter.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <filesystem>

namespace Canape
{
	std::shared_ptr<Mesh> ResourceManager::LoadMesh(const std::string& path)
	{
		const std::string key = std::filesystem::path(path).lexically_normal().generic_string();

		if (const auto found = m_Meshes.find(key); found != m_Meshes.end())
		{
			return found->second;
		}

		std::string error;
		std::shared_ptr<Mesh> mesh = FbxImporter::LoadMesh(path, &error);
		if (!mesh)
		{
			OutputDebugStringA(("[ResourceManager] Failed to load mesh: " + error + "\n").c_str());
			return nullptr;
		}

		m_Meshes.emplace(key, mesh);
		return mesh;
	}

	void ResourceManager::Clear()
	{
		m_Meshes.clear();
	}
}
