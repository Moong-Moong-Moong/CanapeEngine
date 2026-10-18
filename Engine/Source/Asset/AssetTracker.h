#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace Canape
{
	enum class AssetType { Mesh, Material, Texture };

	const char* ToString(AssetType type);

	// Issued by AssetTracker per acquired handle. Zero is invalid.
	struct AssetRecordId
	{
		uint64_t value = 0;

		bool IsValid() const { return value != 0; }
	};

	// Snapshot of who acquired an asset, taken at acquisition time. Display only.
	struct AssetOwnerInfo
	{
		std::string sceneName;
		std::string objectName;
		std::string componentName;
	};

	// Debug record of every live asset handle. Records are removed by RecordId,
	// so duplicate or renamed owners never collide.
	class AssetTracker
	{
	public:
		AssetRecordId AddRecord(AssetType type, uint64_t assetId, const std::string& path, const AssetOwnerInfo& owner);
		void DeleteRecord(AssetRecordId recordId);
		// Writes every record grouped by AssetType to the debugger output.
		void PrintRecord() const;

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
