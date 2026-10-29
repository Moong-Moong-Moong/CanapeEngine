#pragma once

#include "Asset/Texture/TextureAsset.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Canape
{
	class TextureImporter
	{
	public:
		// Decodes any WIC-supported image into RGBA8 sRGB BaseColor data. No caching.
		static std::unique_ptr<TextureAsset> LoadTexture(const std::filesystem::path& path, std::string* outError = nullptr);
	};
}
