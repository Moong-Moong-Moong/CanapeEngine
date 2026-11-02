#include "Asset/Material/MaterialFile.h"
#include "CNDebug/DebugLog.h"
#include "ThirdParty/nlohmann/json.hpp"

#include <cstdint>
#include <fstream>
#include <system_error>

namespace Canape
{
	namespace
	{
		using Json = nlohmann::basic_json<nlohmann::ordered_map, std::vector, std::string, bool, std::int64_t, std::uint64_t, float>;

		constexpr int kMaterialFormatVersion = 1;

		const char* ToString(AlphaMode mode)
		{
			switch (mode)
			{
			case AlphaMode::Masked: return "Masked";
			case AlphaMode::Transparent: return "Transparent";
			default: return "Opaque";
			}
		}

		AlphaMode ToAlphaMode(const std::string& text)
		{
			if (text == "Masked") return AlphaMode::Masked;
			if (text == "Transparent") return AlphaMode::Transparent;
			return AlphaMode::Opaque;
		}

		float ReadFloat(const Json& json, const char* key, float fallback)
		{
			const auto found = json.find(key);
			return found != json.end() && found->is_number() ? found->get<float>() : fallback;
		}
	}

	namespace MaterialFile
	{
		bool IsMaterialPath(const std::string& path)
		{
			const std::filesystem::path extension = std::filesystem::path(std::u8string(path.begin(), path.end())).extension();
			const std::u8string text = extension.u8string();
			if (text.size() != sizeof(kExtension) - 1)
			{
				return false;
			}
			for (size_t i = 0; i < text.size(); ++i)
			{
				char c = static_cast<char>(text[i]);
				if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
				if (c != kExtension[i])
				{
					return false;
				}
			}
			return true;
		}

		bool Load(const std::filesystem::path& file, MaterialFileData& data)
		{
			std::ifstream stream(file, std::ios::binary);
			if (!stream)
			{
				return false;
			}

			const Json root = Json::parse(stream, nullptr, false);
			if (!root.is_object())
			{
				CN_LOG_RUNTIME("Asset", "머티리얼 파일 파싱 실패 : ", file.string());
				return false;
			}

			data = {};
			data.baseColorTexture = root.value("baseColorTexture", std::string());

			const auto desc = root.find("desc");
			if (desc != root.end() && desc->is_object())
			{
				const auto color = desc->find("baseColor");
				if (color != desc->end() && color->is_object())
				{
					data.desc.baseColor = {
						ReadFloat(*color, "X", 1.0f), ReadFloat(*color, "Y", 1.0f),
						ReadFloat(*color, "Z", 1.0f), ReadFloat(*color, "W", 1.0f) };
				}
				data.desc.roughness = ReadFloat(*desc, "roughness", data.desc.roughness);
				data.desc.metallic = ReadFloat(*desc, "metallic", data.desc.metallic);
				data.desc.alphaMode = ToAlphaMode(desc->value("alphaMode", std::string()));
				data.desc.alphaCutoff = ReadFloat(*desc, "alphaCutoff", data.desc.alphaCutoff);
			}
			return true;
		}

		bool Save(const std::filesystem::path& file, const MaterialFileData& data)
		{
			std::error_code error;
			if (file.has_parent_path())
			{
				std::filesystem::create_directories(file.parent_path(), error);
			}

			Json root;
			root["version"] = kMaterialFormatVersion;
			root["baseColorTexture"] = data.baseColorTexture;

			Json desc;
			desc["baseColor"] = {
				{ "X", data.desc.baseColor.X }, { "Y", data.desc.baseColor.Y },
				{ "Z", data.desc.baseColor.Z }, { "W", data.desc.baseColor.W } };
			desc["roughness"] = data.desc.roughness;
			desc["metallic"] = data.desc.metallic;
			desc["alphaMode"] = ToString(data.desc.alphaMode);
			desc["alphaCutoff"] = data.desc.alphaCutoff;
			root["desc"] = std::move(desc);

			std::ofstream stream(file);
			if (!stream)
			{
				CN_LOG_RUNTIME("Asset", "머티리얼 파일 저장 실패 : ", file.string());
				return false;
			}
			stream << root.dump(4) << '\n';
			return static_cast<bool>(stream);
		}
	}
}
