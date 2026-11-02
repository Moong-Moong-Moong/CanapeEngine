#include "EditorUtils.h"

#include <DirectXMath.h>

#include <cmath>

using namespace Canape;

namespace EditorUtils
{
    Vector3 QuaternionToEuler(const Quaternion& rotation)
    {
        const Quaternion q = rotation.Normalized();
        const float sinPitch = 2.0f * (q.W * q.X - q.Y * q.Z);
        const float pitch = std::abs(sinPitch) >= 1.0f ? std::copysign(Math::Pi * 0.5f, sinPitch) : std::asin(sinPitch);
        const float yaw = std::atan2(2.0f * (q.W * q.Y + q.X * q.Z), 1.0f - 2.0f * (q.X * q.X + q.Y * q.Y));
        const float roll = std::atan2(2.0f * (q.W * q.Z + q.X * q.Y), 1.0f - 2.0f * (q.X * q.X + q.Z * q.Z));
        return { Math::ToDegrees(pitch), Math::ToDegrees(yaw), Math::ToDegrees(roll) };
    }

    Quaternion EulerToQuaternion(const Vector3& degrees)
    {
        return Rotator{ degrees.X, degrees.Y, degrees.Z }.ToQuaternion();
    }

    bool CanReparent(const TransformComponent* child, const TransformComponent* parent)
    {
        if (!child || child == parent)
        {
            return false;
        }
        return !parent || !parent->IsChildOf(child);
    }

    bool SetParentKeepWorld(TransformComponent* child, TransformComponent* parent)
    {
        if (!CanReparent(child, parent) || child->GetParent() == parent)
        {
            return false;
        }

        const Matrix4 world = child->GetLocalToWorldMatrix();
        const Matrix4 local = parent ? world * parent->GetWorldToLocalMatrix() : world;

        DirectX::XMVECTOR scale;
        DirectX::XMVECTOR rotation;
        DirectX::XMVECTOR translation;
        if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, local.ToXM()))
        {
            child->SetLocalPosition(Vector3::FromXM(translation));
            child->SetLocalRotation(Quaternion::FromXM(rotation).Normalized());
            child->SetLocalScale(Vector3::FromXM(scale));
        }

        child->SetParent(parent);
        return true;
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return std::string(text.begin(), text.end());
    }

    std::filesystem::path FromUtf8(const std::string& text)
    {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }

    const std::filesystem::path& GetGameDirectory()
    {
        static const std::filesystem::path directory = FromUtf8(CANAPE_GAME_DIR).lexically_normal();
        return directory;
    }

    std::filesystem::path GetSceneDirectory()
    {
        return GetGameDirectory() / "Content" / "Scenes";
    }

    std::string ToGameRelativePath(const std::filesystem::path& path)
    {
        const std::filesystem::path relative = path.lexically_normal().lexically_relative(GetGameDirectory());
        if (relative.empty() || *relative.begin() == "..")
        {
            return ToUtf8(path);
        }
        return ToUtf8(relative);
    }
}
