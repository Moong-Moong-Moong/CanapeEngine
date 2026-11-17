#include "Asset/Animation/AnimationClipSerializer.h"
#include "Asset/Animation/AnimationClipAsset.h"
#include "CNDebug/DebugLog.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace Canape
{
	bool AnimationClipSerializer::Save(const AnimationClipAsset& clip, const std::string& path)
	{
		nlohmann::json root;
		root["name"] = clip.GetName();
		root["duration"] = clip.GetDuration();

		nlohmann::json tracks = nlohmann::json::array();
		for (const AnimationTrack& track : clip.GetTracks())
		{
			nlohmann::json entry;
			entry["targetPath"] = track.targetPath;
			entry["property"] = track.property == AnimationProperty::Translation ? "translation"
				: track.property == AnimationProperty::Rotation ? "rotation" : "scale";
			entry["interpolation"] = track.interpolation == AnimationInterpolation::Step ? "step" : "linear";
			entry["times"] = track.times;

			nlohmann::json values = nlohmann::json::array();
			for (const Vector4& value : track.values)
			{
				values.push_back({ value.X, value.Y, value.Z, value.W });
			}
			entry["values"] = std::move(values);
			tracks.push_back(std::move(entry));
		}
		root["tracks"] = std::move(tracks);

		const std::filesystem::path filePath = std::filesystem::path(std::u8string(path.begin(), path.end()));
		std::error_code error;
		if (filePath.has_parent_path())
		{
			std::filesystem::create_directories(filePath.parent_path(), error);
		}

		std::ofstream file(filePath);
		if (!file)
		{
			CN_LOG_RUNTIME("Asset", "애니메이션 클립 저장 실패 : ", path);
			return false;
		}
		file << root.dump(4);
		return true;
	}

	std::unique_ptr<AnimationClipAsset> AnimationClipSerializer::Load(const std::string& path)
	{
		std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
		if (!file)
		{
			CN_LOG_RUNTIME("Asset", "애니메이션 클립 로드 실패 : 파일을 열 수 없음, ", path);
			return nullptr;
		}

		const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			CN_LOG_RUNTIME("Asset", "애니메이션 클립 로드 실패 : JSON 파싱 오류, ", path);
			return nullptr;
		}

		std::vector<AnimationTrack> tracks;
		for (const nlohmann::json& entry : root.value("tracks", nlohmann::json::array()))
		{
			AnimationTrack track;
			track.targetPath = entry.value("targetPath", std::string());

			const std::string property = entry.value("property", std::string());
			if (property == "translation") track.property = AnimationProperty::Translation;
			else if (property == "rotation") track.property = AnimationProperty::Rotation;
			else if (property == "scale") track.property = AnimationProperty::Scale;
			else continue;

			track.interpolation = entry.value("interpolation", std::string()) == "step"
				? AnimationInterpolation::Step : AnimationInterpolation::Linear;
			track.times = entry.value("times", std::vector<float>());

			for (const nlohmann::json& value : entry.value("values", nlohmann::json::array()))
			{
				if (value.is_array() && value.size() == 4)
				{
					track.values.push_back({ value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>() });
				}
			}

			if (track.times.size() != track.values.size())
			{
				continue;
			}
			tracks.push_back(std::move(track));
		}

		return std::make_unique<AnimationClipAsset>(root.value("name", std::string()), root.value("duration", 0.0f), std::move(tracks));
	}
}
