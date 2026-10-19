#include "Asset/AssetTracker.h"
#include "Debug/DebugLog.h"

#include <iterator>
#include <sstream>
#include <utility>
#include <vector>

namespace Canape
{
	namespace
	{
		constexpr const char* kBranch = "\xE2\x94\x9C\xE2\x94\x80 ";
		constexpr const char* kLastBranch = "\xE2\x94\x94\xE2\x94\x80 ";
		constexpr const char* kPipe = "\xE2\x94\x82  ";
		constexpr const char* kSpace = "   ";
	}

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

	void AssetTracker::PrintRecord(std::span<const AssetStatus> assets) const
	{
		struct AssetNode
		{
			const AssetStatus* status = nullptr;
			std::string path;
			std::vector<std::pair<uint64_t, const Record*>> records;
		};

		constexpr AssetType kTypes[] = { AssetType::Mesh, AssetType::Material, AssetType::Texture };
		constexpr size_t kTypeCount = std::size(kTypes);

		std::map<uint64_t, AssetNode> nodesByType[kTypeCount];
		size_t gpuUploadedCount = 0;
		size_t unusedGpuCount = 0;
		for (const AssetStatus& status : assets)
		{
			AssetNode& node = nodesByType[static_cast<size_t>(status.type)][status.assetId];
			node.status = &status;
			node.path = status.path;
			if (status.gpuUploaded)
			{
				++gpuUploadedCount;
				if (status.referenceCount == 0)
				{
					++unusedGpuCount;
				}
			}
		}
		for (const auto& [recordId, record] : m_Records)
		{
			AssetNode& node = nodesByType[static_cast<size_t>(record.type)][record.assetId];
			if (node.path.empty())
			{
				node.path = record.path;
			}
			node.records.emplace_back(recordId, &record);
		}

		std::ostringstream stream;
		stream << "Loaded assets: " << assets.size() << ", Live handles: " << m_Records.size()
			<< ", GPU allocated: " << gpuUploadedCount << " (unused: " << unusedGpuCount << ")\n";

		for (size_t typeIndex = 0; typeIndex < kTypeCount; ++typeIndex)
		{
			const std::map<uint64_t, AssetNode>& nodes = nodesByType[typeIndex];
			const bool lastType = typeIndex + 1 == kTypeCount;

			size_t handleCount = 0;
			size_t typeGpuCount = 0;
			for (const auto& [assetId, node] : nodes)
			{
				handleCount += node.records.size();
				if (node.status && node.status->gpuUploaded)
				{
					++typeGpuCount;
				}
			}

			stream << (lastType ? kLastBranch : kBranch) << ToString(kTypes[typeIndex])
				<< " (assets: " << nodes.size() << ", handles: " << handleCount << ", GPU: " << typeGpuCount << ")\n";

			const char* typeIndent = lastType ? kSpace : kPipe;
			size_t assetIndex = 0;
			for (const auto& [assetId, node] : nodes)
			{
				const bool lastAsset = ++assetIndex == nodes.size();
				const auto& records = node.records;
				const uint32_t referenceCount = node.status ? node.status->referenceCount : static_cast<uint32_t>(records.size());

				stream << typeIndent << (lastAsset ? kLastBranch : kBranch)
					<< "Asset " << assetId << "  '" << node.path << "' ";

				stream << "(handles: " << referenceCount << ") ";
				if (!node.status)
				{
					stream << "[GPU: Unknown]";
				}
				else if (node.status->gpuUploaded)
				{
					stream << "[GPU: Allocated]";
				}
				else
				{
					stream << "[GPU: None]";
				}

				if (node.status && node.status->gpuUploaded && referenceCount == 0)
				{
					stream << "  <- Unused";
				}
				stream << '\n';

				const char* assetIndent = lastAsset ? kSpace : kPipe;
				for (size_t recordIndex = 0; recordIndex < records.size(); ++recordIndex)
				{
					const bool lastRecord = recordIndex + 1 == records.size();
					const AssetOwnerInfo& owner = records[recordIndex].second->owner;
					stream << typeIndent << assetIndent << (lastRecord ? kLastBranch : kBranch)
						<< "Handle #" << records[recordIndex].first << "  "
						<< owner.sceneName << " / " << owner.objectName << " / " << owner.componentName << '\n';
				}
			}
		}

		CN_LOG_ENGINE("AssetTracker", stream.str());
	}
}
