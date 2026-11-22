#include "Audio/AudioSourceComponent.h"
#include "Audio/AudioSystem.h"
#include "Scene/Component/TransformComponent.h"

namespace Canape
{
	AudioSourceComponent::~AudioSourceComponent()
	{
		if (AudioSystem* audio = AudioSystem::Get())
		{
			audio->ReleaseSource(*this);
		}
	}

	void AudioSourceComponent::Play()
	{
		if (AudioSystem* audio = AudioSystem::Get())
		{
			audio->PlaySource(*this);
		}
	}

	void AudioSourceComponent::Stop()
	{
		if (AudioSystem* audio = AudioSystem::Get())
		{
			audio->StopSource(*this);
		}
	}

	bool AudioSourceComponent::IsPlaying() const
	{
		const AudioSystem* audio = AudioSystem::Get();
		return audio && audio->IsSourcePlaying(*this);
	}

	void AudioSourceComponent::PlayOneShot(const std::string& clip, float volumeScale) const
	{
		AudioSystem* audio = AudioSystem::Get();
		if (!audio)
		{
			return;
		}
		if (m_Spatial)
		{
			audio->PlayOneShot(clip, GetTransform()->GetWorldPosition(), m_Volume * volumeScale, m_Pitch, m_MinDistance, m_MaxDistance);
		}
		else
		{
			audio->Play2D(clip, m_Volume * volumeScale, m_Pitch);
		}
	}
}
