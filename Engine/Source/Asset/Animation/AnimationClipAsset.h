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
        std::string targetPath;              // 모델 루트 기준 노드 경로 ("Bone0/Bone1", 루트 자신은 "")
        AnimationProperty property = AnimationProperty::Translation;
        AnimationInterpolation interpolation = AnimationInterpolation::Linear;
        std::vector<float> times;            // 초 단위, 오름차순
        std::vector<Vector4> values;           // T/S: 키당 3개, R: 키당 4개 (x,y,z,w), 엔진 좌표계로 변환된 값

        uint32_t GetComponentCount() const { return property == AnimationProperty::Rotation ? 4u : 3u; }
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
        float m_Duration = 0.0f;             // 모든 트랙 times의 최댓값
        std::vector<AnimationTrack> m_Tracks;
    };
}