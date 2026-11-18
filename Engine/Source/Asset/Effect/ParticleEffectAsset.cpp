#include "Asset/Effect/ParticleEffectAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "CNDebug/DebugLog.h"

namespace
{
    constexpr std::uint32_t MaxParticleCount = 65536;

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
        std::uint32_t particleCount,
        std::vector<ParticleEffectParameter> parameters
    )
        : shaderPath(std::move(shaderPath))
        , particleCount(particleCount)
        , parameters(std::move(parameters)) {}

    std::unique_ptr<ParticleEffectAsset> ParticleEffectAsset::Load(const std::string& path)
    {
        std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
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

        return std::make_unique<ParticleEffectAsset>(
            shader->get<std::string>(),
            static_cast<std::uint32_t>(
                std::clamp<std::uint64_t>(count->get<std::uint64_t>(), 1, MaxParticleCount)
            ),
            std::move(parameters)
        );
    }

    ParticleEffectAssetId ParticleEffectAsset::allocateAssetId()
    {
        static std::uint64_t nextId = 1;
        if(nextId == 0) throw std::overflow_error("ParticleEffect asset ID space exhausted");

        return ParticleEffectAssetId{ nextId++ };
    }
}
