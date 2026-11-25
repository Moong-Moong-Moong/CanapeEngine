#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Canape
{
	class MeshAsset;

	class MeshSerializer final
	{
	public:
		static constexpr const char* StaticExtension = ".cnmesh";
		static constexpr const char* SkinnedExtension = ".cnskmesh";
		static constexpr uint32_t Version = 1;

		static bool IsStaticMeshPath(const std::string& path);
		static bool IsSkinnedMeshPath(const std::string& path);
		static const char* ExtensionFor(const MeshAsset& mesh);

		static bool Save(const MeshAsset& mesh, const std::string& path);
		static bool ReadMaterialSlots(const std::string& path, std::vector<std::string>& slots);
		static bool WriteMaterialSlots(const std::string& path, const std::vector<std::string>& slots);
		static std::unique_ptr<MeshAsset> Load(const std::string& path);
	};
}
