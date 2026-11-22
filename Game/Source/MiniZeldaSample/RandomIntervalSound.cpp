#include "MiniZeldaSample/RandomIntervalSound.h"

#include <algorithm>
#include <random>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<RandomIntervalSound>("RandomIntervalSound")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("clip", &RandomIntervalSound::m_Clip)
        .property("minInterval", &RandomIntervalSound::m_MinInterval)
        .property("maxInterval", &RandomIntervalSound::m_MaxInterval)
        .property("volume", &RandomIntervalSound::m_Volume)
        .property("spatial", &RandomIntervalSound::m_Spatial)
        .property("minDistance", &RandomIntervalSound::m_MinDistance)
        .property("maxDistance", &RandomIntervalSound::m_MaxDistance);
}

float RandomIntervalSound::NextInterval() const
{
    static std::mt19937 rng{ std::random_device{}() };
    const float low = std::max(0.1f, std::min(m_MinInterval, m_MaxInterval));
    const float high = std::max(low, std::max(m_MinInterval, m_MaxInterval));
    return std::uniform_real_distribution<float>(low, high)(rng);
}

void RandomIntervalSound::Start()
{
    m_Timer = NextInterval();
}

void RandomIntervalSound::Update()
{
    m_Timer -= Time::DeltaTime();
    if (m_Timer > 0.0f)
    {
        return;
    }
    m_Timer = NextInterval();

    AudioSystem* audio = AudioSystem::Get();
    if (!audio)
    {
        return;
    }
    if (m_Spatial)
    {
        audio->PlayOneShot(m_Clip, GetTransform()->GetWorldPosition(), m_Volume, 1.0f, m_MinDistance, m_MaxDistance);
    }
    else
    {
        audio->Play2D(m_Clip, m_Volume);
    }
}
