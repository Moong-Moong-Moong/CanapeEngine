#pragma once
#include "Asset/AssetId.h"
#include "Math/Vector4.h"
#include <string>
#include <vector>

namespace Canape
{
    enum class AnimationProperty : uint8_t { Translation, Rotation, Scale };
    enum class AnimationInterpolation : uint8_t { Step, Linear };   

    struct AnimationTrack
    {
        std::string targetPath;              // skeleton 오브젝트의 루트 오브젝트 기준 상대 경로
        AnimationProperty property = AnimationProperty::Translation;
        AnimationInterpolation interpolation = AnimationInterpolation::Linear;
        std::vector<float> times;            // 초 단위, 오름차순
        std::vector<Vector4> values;       

        uint32_t GetKeyCount() const { return static_cast<uint32_t>(times.size()); }
    };

    class AnimationClipAsset
    {
    public:
        AnimationClipAssetId GetAssetId() const { return m_AssetId; }
        AnimationClipAsset() = default;
        AnimationClipAsset(std::string name, float duration, std::vector<AnimationTrack> tracks)
            : m_Name(std::move(name)), m_Duration(duration), m_Tracks(std::move(tracks)) {}

        AnimationClipAsset(const AnimationClipAsset&) = delete;
        AnimationClipAsset& operator=(const AnimationClipAsset&) = delete;

        const std::string& GetName() const { return m_Name; }
        float GetDuration() const { return m_Duration; }
        const std::vector<AnimationTrack>& GetTracks() const { return m_Tracks; }

    private:
        static AnimationClipAssetId AllocateAssetId();
        const AnimationClipAssetId m_AssetId = AllocateAssetId();
        std::string m_Name;
        float m_Duration = 0.0f;             // 트랙중 가장 긴 time 값을 사용
        std::vector<AnimationTrack> m_Tracks;
    };
}