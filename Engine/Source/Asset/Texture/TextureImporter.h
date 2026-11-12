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
		// Decodes any WIC-supported image into RGBA8 data. No caching.
		// format: sRGB for color textures, UNORM for data textures (normal, ORM).
		static std::unique_ptr<TextureAsset> LoadTexture(const std::filesystem::path& path, std::string* outError = nullptr,
			TextureFormat format = TextureFormat::RGBA8_SRGB);
	};
}
