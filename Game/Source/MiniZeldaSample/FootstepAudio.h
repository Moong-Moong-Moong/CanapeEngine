#pragma once

#include <Engine.h>

#include <string>
#include <vector>

// 걷기/뛰기 애니메이션이 발을 디디는 순간마다 발소리를 낸다. 플레이어에 붙인다.
// - animatorName 오브젝트의 Animator가 walkClip / runClip을 재생 중일 때만 동작한다.
// - 한 주기(0~1) 안에서 stepPhaseA, stepPhaseB를 지나는 순간이 발 디딤이다.
// - clips 중 하나를 무작위로(직전과 다른 것) 플레이어 위치에서 3D로 재생한다. 볼륨/피치는 조금씩 흔든다.
class FootstepAudio final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

protected:
    void Update() override;

private:
    void PlayStep(bool running);

    //***** Authoring
    std::string m_AnimatorName = "Knight";
    std::string m_WalkClip = "Content/Models/Knight/walk.aclip";
    std::string m_RunClip = "Content/Models/Knight/run.aclip";
    std::vector<std::string> m_Clips{
        "Content/Audio/Footstep_Grass_01.wav",
        "Content/Audio/Footstep_Grass_05.wav",
        "Content/Audio/Footstep_Grass_10.wav",
    };
    float m_StepPhaseA = 0.0f;   // 애니메이션 주기에서 한쪽 발이 닿는 지점(0~1)
    float m_StepPhaseB = 0.5f;   // 다른 쪽 발
    float m_WalkVolume = 0.6f;
    float m_RunVolume = 0.85f;
    float m_PitchJitter = 0.08f; // 피치를 1 ± 이만큼 무작위로
    float m_MinDistance = 1.5f;
    float m_MaxDistance = 20.0f;

    //***** Runtime (직렬화 안 함)
    std::string m_LastClip;
    float m_LastPhase = -1.0f;
    int m_LastSound = -1;
};
