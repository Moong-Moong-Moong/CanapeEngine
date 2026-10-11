#pragma once

#include "Renderer/Mesh.h"
#include "Renderer/TextureAsset.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Canape
{
	class ResourceManager
	{
	public:
		std::shared_ptr<Mesh> LoadMesh(const std::string& path);
		// UTF-8 path; images are converted to RGBA8 and interpreted as sRGB BaseColor data.
		std::shared_ptr<TextureAsset> LoadTexture(const std::string& path, std::string* outError = nullptr);
		void Clear();

	private:
		std::unordered_map<std::string, std::shared_ptr<Mesh>> m_Meshes;
		std::unordered_map<std::string, std::shared_ptr<TextureAsset>> m_Textures;
	};
}
