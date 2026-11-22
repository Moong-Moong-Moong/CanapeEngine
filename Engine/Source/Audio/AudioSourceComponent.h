#pragma once

#include "Scene/Component/Component.h"

#include <string>

namespace Canape
{
	//***** 소리를 내는 오브젝트에 붙인다. 오브젝트의 위치가 곧 음원 위치다(spatial일 때).
	//*** playOnStart면 플레이가 시작될 때 재생한다(loop와 같이 쓰면 모닥불·환경음).
	//*** 스크립트에서는 Play / Stop / PlayOneShot(다른 클립을 이 위치에서 한 번)을 쓴다.
	class AudioSourceComponent final : public Component
	{
		RTTR_ENABLE(Component)

	public:
		AudioSourceComponent() = default;
		~AudioSourceComponent() override;

		//***** Authoring (Inspector)
		const std::string& GetClip() const { return m_Clip; }
		void SetClip(const std::string& clip) { m_Clip = clip; }
		float GetVolume() const { return m_Volume; }
		void SetVolume(float volume) { m_Volume = volume; }
		float GetPitch() const { return m_Pitch; }
		void SetPitch(float pitch) { m_Pitch = pitch; }
		bool GetLoop() const { return m_Loop; }
		void SetLoop(bool loop) { m_Loop = loop; }
		bool GetPlayOnStart() const { return m_PlayOnStart; }
		void SetPlayOnStart(bool play) { m_PlayOnStart = play; }
		// false면 거리/방향 없이 들린다(BGM, UI)
		bool GetSpatial() const { return m_Spatial; }
		void SetSpatial(bool spatial) { m_Spatial = spatial; }
		// 이 거리(m) 안은 최대 볼륨, maxDistance에서 0
		float GetMinDistance() const { return m_MinDistance; }
		void SetMinDistance(float distance) { m_MinDistance = distance; }
		float GetMaxDistance() const { return m_MaxDistance; }
		void SetMaxDistance(float distance) { m_MaxDistance = distance; }

		//***** Runtime
		void Play();
		void Stop();
		bool IsPlaying() const;
		// clip을 이 오브젝트 위치(또는 2D)에서 한 번 재생한다. 이 소스의 볼륨/거리 설정을 쓴다.
		void PlayOneShot(const std::string& clip, float volumeScale = 1.0f) const;

		// AudioSystem이 playOnStart 처리에 쓴다.
		bool HasAutoStarted() const { return m_AutoStarted; }
		void MarkAutoStarted(bool started) { m_AutoStarted = started; }

	private:
		std::string m_Clip;
		float m_Volume = 1.0f;
		float m_Pitch = 1.0f;
		bool m_Loop = false;
		bool m_PlayOnStart = false;
		bool m_Spatial = true;
		float m_MinDistance = 1.0f;
		float m_MaxDistance = 20.0f;

		bool m_AutoStarted = false; // 직렬화 안 함
	};
}
