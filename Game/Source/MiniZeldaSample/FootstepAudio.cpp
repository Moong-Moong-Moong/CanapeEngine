#include "MiniZeldaSample/FootstepAudio.h"

#include "Scene/Component/AnimatorComponent.h"

#include <cmath>
#include <random>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<FootstepAudio>("FootstepAudio")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("animatorName", &FootstepAudio::m_AnimatorName)
        .property("walkClip", &FootstepAudio::m_WalkClip)
        .property("runClip", &FootstepAudio::m_RunClip)
        .property("clips", &FootstepAudio::m_Clips)
        .property("stepPhaseA", &FootstepAudio::m_StepPhaseA)
        .property("stepPhaseB", &FootstepAudio::m_StepPhaseB)
        .property("walkVolume", &FootstepAudio::m_WalkVolume)
        .property("runVolume", &FootstepAudio::m_RunVolume)
        .property("pitchJitter", &FootstepAudio::m_PitchJitter)
        .property("minDistance", &FootstepAudio::m_MinDistance)
        .property("maxDistance", &FootstepAudio::m_MaxDistance);
}

namespace
{
    std::mt19937& Rng()
    {
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
    }

    // last에서 now로 진행하는 동안(한 주기 안에서 감기는 경우 포함) phase를 지났는가
    bool Crossed(float last, float now, float phase)
    {
        if (now >= last) return last < phase && phase <= now;
        return phase > last || phase <= now; // 주기가 한 바퀴 돌았다
    }
}

void FootstepAudio::PlayStep(bool running)
{
    AudioSystem* audio = AudioSystem::Get();
    if (!audio || m_Clips.empty())
    {
        return;
    }
    // 직전과 다른 소리를 고른다.
    int index = 0;
    if (m_Clips.size() > 1)
    {
        std::uniform_int_distribution<int> pick(0, static_cast<int>(m_Clips.size()) - 2);
        index = pick(Rng());
        if (index >= m_LastSound && m_LastSound >= 0) ++index;
    }
    m_LastSound = index;

    std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
    const float pitch = 1.0f + m_PitchJitter * jitter(Rng());
    const float volume = (running ? m_RunVolume : m_WalkVolume) * (1.0f + 0.1f * jitter(Rng()));
    audio->PlayOneShot(m_Clips[index], GetTransform()->GetWorldPosition(), volume, pitch, m_MinDistance, m_MaxDistance);
}

void FootstepAudio::Update()
{
    GameObject* animatorObject = FindGameObjectByName(m_AnimatorName);
    const auto* animator = animatorObject ? animatorObject->GetComponent<AnimatorComponent>() : nullptr;
    const std::string clip = animator ? animator->GetClipPath() : std::string();
    const bool walking = clip == m_WalkClip;
    const bool running = clip == m_RunClip;
    if (!animator || !animator->IsPlaying() || (!walking && !running) || animator->GetDuration() <= 0.0f)
    {
        m_LastPhase = -1.0f;
        m_LastClip.clear();
        return;
    }

    const float phase = std::fmod(animator->GetTime() / animator->GetDuration(), 1.0f);
    if (m_LastPhase >= 0.0f && clip == m_LastClip)
    {
        if (Crossed(m_LastPhase, phase, m_StepPhaseA) || Crossed(m_LastPhase, phase, m_StepPhaseB))
        {
            PlayStep(running);
        }
    }
    m_LastPhase = phase;
    m_LastClip = clip;
}
