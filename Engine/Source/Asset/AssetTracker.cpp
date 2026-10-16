#include "Asset/AssetTracker.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <sstream>

namespace Canape
{
	const char* ToString(AssetType type)
	{
		switch (type)
		{
		case AssetType::Mesh: return "Mesh";
		case AssetType::Material: return "Material";
		case AssetType::Texture: return "Texture";
		}
		return "Unknown";
	}

	AssetRecordId AssetTracker::AddRecord(AssetType type, uint64_t assetId, const std::string& path, const AssetOwnerInfo& owner)
	{
		const AssetRecordId recordId{ m_NextRecordId++ };
		m_Records.emplace(recordId.value, Record{ type, assetId, path, owner });
		return recordId;
	}

	void AssetTracker::DeleteRecord(AssetRecordId recordId)
	{
		m_Records.erase(recordId.value);
	}

	void AssetTracker::PrintRecord() const
	{
		std::ostringstream stream;
		stream << "[AssetTracker] " << m_Records.size() << " live asset handle(s)\n";
		for (const AssetType type : { AssetType::Mesh, AssetType::Material, AssetType::Texture })
		{
			bool printedHeader = false;
			for (const auto& [recordId, record] : m_Records)
			{
				if (record.type != type)
				{
					continue;
				}
				if (!printedHeader)
				{
					stream << "  " << ToString(type) << '\n';
					printedHeader = true;
				}
				stream << "    #" << recordId << " asset " << record.assetId << " '" << record.path << "' <- "
					<< record.owner.sceneName << " / " << record.owner.objectName << " / " << record.owner.componentName << '\n';
			}
		}
		OutputDebugStringA(stream.str().c_str());
	}
}
