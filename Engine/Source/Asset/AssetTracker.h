#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>

namespace Canape
{
	enum class AssetType { Mesh, Material, Texture };

	const char* ToString(AssetType type);

	struct AssetRecordId
	{
		uint64_t value = 0;

		bool IsValid() const { return value != 0; }
	};

	struct AssetOwnerInfo
	{
		std::string sceneName;
		std::string objectName;
		std::string componentName;
	};

	struct AssetStatus
	{
		AssetType type;
		uint64_t assetId = 0;
		std::string path;
		uint32_t referenceCount = 0;
		bool gpuUploaded = false;
	};

	class AssetTracker
	{
	public:
		AssetRecordId AddRecord(AssetType type, uint64_t assetId, const std::string& path, const AssetOwnerInfo& owner);
		void DeleteRecord(AssetRecordId recordId);
		void PrintRecord(std::span<const AssetStatus> assets) const;

		size_t GetRecordCount() const { return m_Records.size(); }

	private:
		struct Record
		{
			AssetType type;
			uint64_t assetId;
			std::string path;
			AssetOwnerInfo owner;
		};

		std::map<uint64_t, Record> m_Records;
		uint64_t m_NextRecordId = 1;
	};
}
