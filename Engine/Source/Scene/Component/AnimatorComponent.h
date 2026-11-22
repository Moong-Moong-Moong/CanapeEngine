#pragma once
#include "Scene/Component/Component.h"
#include "Asset/AssetManager.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Canape
{
    class TransformComponent;

    class AnimatorComponent : public Component
    {
        RTTR_ENABLE(Component)

    public:
        AnimatorComponent() = default;

        bool SetClip(const std::string& clipPath);
        const std::string& GetClipPath() const { return m_ClipPath; }
        void SetClipPath(const std::string& clipPath) { SetClip(clipPath); }

        void Play(bool loop = true) { m_Playing = true; m_Loop = loop; }
        void Stop() { m_Playing = false; m_Time = 0.0f; }
        void SetSpeed(float speed) { m_Speed = speed; }

        void ApplyKeyFrame(float deltaTime);

        float GetTime() const { return m_Time; }
        float GetDuration() const;
        bool IsPlaying() const { return m_Playing; }

        bool GetRootMotion() const { return m_rootMotion; }
        void SetRootMotion(bool rootMotion) { m_rootMotion = rootMotion; }

        // 미리 로드할 클립 목록(직렬화). 씬이 시작되고 처음 갱신될 때 전부 로드하고 본 바인딩까지 만들어 두어,
        // 플레이 중 SetClip으로 처음 바꿀 때 디스크 로드/바인딩 때문에 프레임이 튀지 않게 한다.
        const std::vector<std::string>& GetPreloadClips() const { return m_PreloadClips; }
        void SetPreloadClips(const std::vector<std::string>& clips) { m_PreloadClips = clips; m_Preloaded = false; }
        // 목록의 클립을 지금 로드하고 바인딩한다(보통은 첫 ApplyKeyFrame에서 자동으로 불린다).
        void PreloadClips();

    private:
        struct TrackTarget
        {
            TransformComponent* transform = nullptr;
            bool isTranslationRoot = false;
        };

        // 클립 하나의 핸들과 트랙 대상. 한 번 만든 바인딩은 클립을 바꿨다 돌아와도 다시 쓴다.
        struct ClipBinding
        {
            AnimationClipAssetHandle clip;
            std::vector<TrackTarget> targets;          // tracks[i]의 대상, 못 찾으면 nullptr. 비어 있으면 아직 바인딩 전
        };

        ClipBinding* FindOrLoad(const std::string& clipPath);
        void Bind(const AnimationClipAsset& clip, std::vector<TrackTarget>& targets);

        std::string m_ClipPath;                        // 직렬화 대상
        std::vector<std::string> m_PreloadClips;       // 직렬화 대상
        std::unordered_map<std::string, ClipBinding> m_Bindings; // 클립 경로 -> 바인딩(요소 주소는 재해시에도 유지)
        ClipBinding* m_Current = nullptr;
        std::vector<uint32_t> m_LastKeys;              // tracks[i]의 마지막 구간 캐시
        bool m_Preloaded = false;
        float m_Time = 0.0f;
        float m_Speed = 1.0f;
        bool m_Loop = true;
        bool m_Playing = true;
        bool m_rootMotion = false;
    };
}
