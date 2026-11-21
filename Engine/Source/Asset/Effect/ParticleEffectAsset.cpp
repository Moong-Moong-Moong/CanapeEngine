#include "Asset/Effect/ParticleEffectAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "Asset/Texture/TextureImporter.h"
#include "CNDebug/DebugLog.h"

namespace
{
    constexpr std::uint32_t MaxParticleCount = 65536;
    constexpr std::uint64_t MaxVerticesPerParticle = 1024;

    std::optional<Canape::ParticleEffectPass> readPass(const nlohmann::json& json)
    {
        using enum Canape::ParticleEffectPass;

        if(json == "opaque") return Opaque;
        if(json == "additive") return Additive;
        return std::nullopt;
    }

    // A positive multiple of three, not above MaxVerticesPerParticle.
    std::optional<std::uint32_t> readVerticesPerParticle(const nlohmann::json& json)
    {
        if(!json.is_number_unsigned()) return std::nullopt;

        const auto vertices = json.get<std::uint64_t>();
        if(vertices == 0 || vertices % 3 != 0 || vertices > MaxVerticesPerParticle) return std::nullopt;

        return static_cast<std::uint32_t>(vertices);
    }

    // A number, or an array of numbers.
    bool readValues(const nlohmann::json& json, std::vector<float>& values)
    {
        if(json.is_number())
        {
            values = { json.get<float>() };
            return true;
        }

        const bool numbers = json.is_array()
            && std::ranges::all_of(json, [](const nlohmann::json& element) { return element.is_number(); });
        if(!numbers) return false;

        values = json.get<std::vector<float>>();
        return true;
    }
}

namespace Canape
{
    ParticleEffectAsset::ParticleEffectAsset(
        std::string shaderPath,
        ParticleEffectPass pass,
        std::uint32_t verticesPerParticle,
        std::uint32_t particleCount,
        std::vector<ParticleEffectParameter> parameters,
        std::unique_ptr<TextureAsset> map
    )
        : shaderPath(std::move(shaderPath))
        , pass(pass)
        , verticesPerParticle(verticesPerParticle)
        , particleCount(particleCount)
        , parameters(std::move(parameters))
        , map(std::move(map)) {}

    std::unique_ptr<ParticleEffectAsset> ParticleEffectAsset::Load(const std::string& path)
    {
        const std::filesystem::path filePath(std::u8string(path.begin(), path.end()));
        std::ifstream file(filePath, std::ios::binary);
        if(!file)
        {
            CN_LOG_RUNTIME("Asset", "파티클 이펙트 로드 실패 : 파일을 열 수 없음, ", path);
            return nullptr;
        }

        const auto root = nlohmann::json::parse(file, nullptr, false);
        const auto shader = root.is_object() ? root.find("shader") : root.end();
        const auto count = root.is_object() ? root.find("count") : root.end();
        const bool described = shader != root.end() && shader->is_string()
            && count != root.end() && count->is_number_unsigned();
        if(!described)
        {
            CN_LOG_RUNTIME("Asset", "파티클 이펙트 로드 실패 : shader 문자열과 count 양의 정수가 필요함, ", path);
            return nullptr;
        }

        const auto pass = readPass(root.value("pass", nlohmann::json{}));
        const auto verticesPerParticle = readVerticesPerParticle(root.value("vertices", nlohmann::json{}));
        if(!pass || !verticesPerParticle)
        {
            CN_LOG_RUNTIME(
                "Asset",
                "파티클 이펙트 로드 실패 : pass는 \"opaque\"나 \"additive\", vertices는 ",
                MaxVerticesPerParticle,
                " 이하의 3의 배수여야 함, ",
                path
            );
            return nullptr;
        }

        std::vector<ParticleEffectParameter> parameters;
        for(const auto& [name, json] : root.value("parameters", nlohmann::json::object()).items())
        {
            ParticleEffectParameter parameter{ .name = name };
            if(!readValues(json, parameter.values))
            {
                CN_LOG_RUNTIME("Asset", "파티클 이펙트 로드 실패 : 파라미터 ", name, "은 숫자나 숫자 배열이어야 함, ", path);
                return nullptr;
            }
            parameters.push_back(std::move(parameter));
        }

        std::unique_ptr<TextureAsset> map;
        if(const auto mapName = root.find("map"); mapName != root.end())
        {
            // The map lies next to the .effect file.
            std::string error = "map은 문자열이어야 함";
            if(mapName->is_string())
            {
                const auto name = mapName->get<std::string>();
                map = TextureImporter::LoadTexture(
                    filePath.parent_path() / std::u8string(name.begin(), name.end()),
                    &error,
                    TextureFormat::RGBA8_UNORM
                );
            }
            if(map == nullptr)
            {
                CN_LOG_RUNTIME("Asset", "파티클 이펙트 로드 실패 : map을 읽을 수 없음(", error, "), ", path);
                return nullptr;
            }
        }

        return std::make_unique<ParticleEffectAsset>(
            shader->get<std::string>(),
            *pass,
            *verticesPerParticle,
            static_cast<std::uint32_t>(
                std::clamp<std::uint64_t>(count->get<std::uint64_t>(), 1, MaxParticleCount)
            ),
            std::move(parameters),
            std::move(map)
        );
    }

    ParticleEffectAssetId ParticleEffectAsset::allocateAssetId()
    {
        static std::uint64_t nextId = 1;
        if(nextId == 0) throw std::overflow_error("ParticleEffect asset ID space exhausted");

        return ParticleEffectAssetId{ nextId++ };
    }
}
