#pragma once

#include "DX11Renderer/Data/MaterialDesc.h"

#include <filesystem>
#include <string>

namespace Canape
{
	struct MaterialFileData
	{
		MaterialDesc desc;
		std::string baseColorTexture;
	};

	namespace MaterialFile
	{
		inline constexpr char kExtension[] = ".mat";

		bool IsMaterialPath(const std::string& path);
		bool Load(const std::filesystem::path& file, MaterialFileData& data);
		bool Save(const std::filesystem::path& file, const MaterialFileData& data);
	}
}
