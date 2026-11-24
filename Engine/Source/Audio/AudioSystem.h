#pragma once

#include "Math/Vector3.h"

#include <memory>
#include <string>

namespace Canape
{
	class AudioSourceComponent;
	class Scene;

	//***** miniaudio 기반 오디오. Application이 하나 갖고 매 프레임 Update한다.
	//*** 리스너는 씬의 메인 카메라(석판/연출로 카메라가 바뀌면 그 카메라)를 따라간다.
	//*** 씬 스크립트가 도는 동안(게임, 에디터 Play)에만 소리를 낸다. 에디터 편집 중에는 모두 멈춘다.
	//*** 경로는 에셋 루트 기준("Content/Audio/xxx.wav"). WAV/MP3/FLAC.
	class AudioSystem
	{
	public:
		AudioSystem();
		~AudioSystem();

		AudioSystem(const AudioSystem&) = delete;
		AudioSystem& operator=(const AudioSystem&) = delete;

		// 살아 있는 오디오 시스템. 없거나(초기화 실패 포함) 종료 중이면 nullptr.
		static AudioSystem* Get();
		bool IsAvailable() const;

		void Update(Scene* scene, bool playing);

		//*** 한 번 재생하고 끝나면 알아서 정리되는 소리
		// 3D: position에서 난다. minDistance 안은 최대 볼륨, maxDistance에서 0이 되도록 선형으로 줄어든다.
		void PlayOneShot(const std::string& clip, const Vector3& position, float volume = 1.0f, float pitch = 1.0f,
			float minDistance = 1.0f, float maxDistance = 30.0f);
		// 2D: 거리와 방향 없이 그대로 들린다(UI, 알림음).
		void Play2D(const std::string& clip, float volume = 1.0f, float pitch = 1.0f);

		//*** AudioSourceComponent용
		void PlaySource(AudioSourceComponent& source);
		void StopSource(AudioSourceComponent& source);
		bool IsSourcePlaying(const AudioSourceComponent& source) const;
		void ReleaseSource(const AudioSourceComponent& source);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
