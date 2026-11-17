#pragma once
#include "Scene/Component/Component.h"
#include "Asset/AssetManager.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Canape
{
    class TransformComponent;

    // 클립 키프레임을 샘플링해서 대상 GameObject들의 로컬 TRS를 덮어쓴다.
    // 트랙의 targetPath는 이 컴포넌트가 붙은 GameObject(모델 루트) 기준 경로다.
    class AnimatorComponent : public Component
    {
        RTTR_ENABLE(Component)

    public:
        AnimatorComponent() = default;

        // 예: "Models/Arm_Bone_Skin_Ani.gltf#0"
        bool SetClip(const std::string& clipPath);
        const std::string& GetClipPath() const { return m_ClipPath; }
        void SetClipPath(const std::string& clipPath) { SetClip(clipPath); }

        void Play(bool loop = true) { m_Playing = true; m_Loop = loop; }
        void Stop() { m_Playing = false; m_Time = 0.0f; }
        void SetSpeed(float speed) { m_Speed = speed; }

        void Evaluate(float deltaTime);

        float GetTime() const { return m_Time; }
        bool IsPlaying() const { return m_Playing; }

    private:
        void Bind(const AnimationClipAsset& clip);

        std::string m_ClipPath;                        // 직렬화 대상
        AnimationClipAssetHandle m_Clip;               
        std::vector<TransformComponent*> m_Targets;    // tracks[i]의 대상, 못 찾으면 nullptr
        std::vector<uint32_t> m_LastKeys;              // tracks[i]의 마지막 구간 캐시
        float m_Time = 0.0f;
        float m_Speed = 1.0f;
        bool m_Loop = true;
        bool m_Playing = true;
    };
}
