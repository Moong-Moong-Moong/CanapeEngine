#pragma once

#include "Asset/Texture/CubemapAsset.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Canape
{
	class CubemapImporter
	{
	public:
		static std::unique_ptr<CubemapAsset> LoadCubemap(const std::filesystem::path& path, std::string* outError = nullptr);
	};
}
