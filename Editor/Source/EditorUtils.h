#pragma once

#include <Engine.h>

#include <filesystem>
#include <string>

namespace EditorUtils
{
    inline constexpr char kGameObjectPayload[] = "CANAPE_GAMEOBJECT";
    inline constexpr char kAssetPathPayload[] = "CANAPE_ASSET_PATH";

    Canape::Vector3 QuaternionToEuler(const Canape::Quaternion& rotation);
    Canape::Quaternion EulerToQuaternion(const Canape::Vector3& degrees);

    bool CanReparent(const Canape::TransformComponent* child, const Canape::TransformComponent* parent);
    bool SetParentKeepWorld(Canape::TransformComponent* child, Canape::TransformComponent* parent);

    std::string ToUtf8(const std::filesystem::path& path);
    std::filesystem::path FromUtf8(const std::string& text);

    const std::filesystem::path& GetGameDirectory();
    std::filesystem::path GetSceneDirectory();
    std::string ToGameRelativePath(const std::filesystem::path& path);
}
