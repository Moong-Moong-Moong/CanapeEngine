#pragma once
#include "MaterialData.h"
#include <filesystem>
#include <string>

namespace Canape
{
	namespace MaterialFile
	{
		inline constexpr char kExtension[] = ".mat";

		bool IsMaterialPath(const std::string& path);
		bool Load(const std::filesystem::path& file, MaterialData& data);
		bool Save(const std::filesystem::path& file, const MaterialData& data);
	}
}
