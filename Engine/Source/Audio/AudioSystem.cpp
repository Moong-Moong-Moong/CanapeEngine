#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#pragma warning(push, 0)
#include <miniaudio.h>
#pragma warning(pop)

#include "Audio/AudioSystem.h"
#include "Audio/AudioSourceComponent.h"
#include "Asset/AssetManager.h"
#include "CNDebug/DebugLog.h"
#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace Canape
{
	namespace
	{
		AudioSystem* s_Instance = nullptr;

		// 엔진은 왼손 좌표계(+Z 앞), miniaudio는 오른손 좌표계(-Z 앞). Z를 뒤집으면 오른쪽(+X)은 그대로다.
		void SetSoundPosition(ma_sound& sound, const Vector3& p) { ma_sound_set_position(&sound, p.X, p.Y, -p.Z); }

		struct SoundSettings
		{
			float volume = 1.0f;
			float pitch = 1.0f;
			bool loop = false;
			bool spatial = true;
			float minDistance = 1.0f;
			float maxDistance = 20.0f;
		};
	}

	struct AudioSystem::Impl
	{
		ma_engine engine{};
		bool ok = false;
		bool playing = false;
		std::vector<std::unique_ptr<ma_sound>> oneShots;
		std::unordered_map<const AudioSourceComponent*, std::unique_ptr<ma_sound>> sources;
		std::vector<std::string> failedClips; // 같은 실패 로그를 반복하지 않게

		std::unique_ptr<ma_sound> Load(const std::string& clip, const SoundSettings& settings)
		{
			if (!ok || clip.empty())
			{
				return nullptr;
			}
			const std::filesystem::path file = AssetManager::GetAssetFile(clip);
			auto sound = std::make_unique<ma_sound>();
			// DECODE: 처음 한 번 메모리로 풀어 두고 같은 파일은 재사용한다(리소스 매니저가 경로로 캐시).
			const ma_result result = ma_sound_init_from_file_w(&engine, file.wstring().c_str(), MA_SOUND_FLAG_DECODE,
				nullptr, nullptr, sound.get());
			if (result != MA_SUCCESS)
			{
				if (std::find(failedClips.begin(), failedClips.end(), clip) == failedClips.end())
				{
					failedClips.push_back(clip);
					CN_LOG_RUNTIME("Audio", "cannot load '", clip, "' (", ma_result_description(result), ")");
				}
				return nullptr;
			}
			ma_sound_set_volume(sound.get(), settings.volume);
			ma_sound_set_pitch(sound.get(), std::max(settings.pitch, 0.01f));
			ma_sound_set_looping(sound.get(), settings.loop ? MA_TRUE : MA_FALSE);
			ma_sound_set_spatialization_enabled(sound.get(), settings.spatial ? MA_TRUE : MA_FALSE);
			if (settings.spatial)
			{
				ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_linear);
				ma_sound_set_min_distance(sound.get(), std::max(settings.minDistance, 0.01f));
				ma_sound_set_max_distance(sound.get(), std::max(settings.maxDistance, settings.minDistance + 0.01f));
			}
			return sound;
		}

		void StopAll()
		{
			for (auto& sound : oneShots) ma_sound_uninit(sound.get());
			oneShots.clear();
			for (auto& [source, sound] : sources) ma_sound_uninit(sound.get());
			sources.clear();
		}
	};

	AudioSystem::AudioSystem()
		: m_Impl(std::make_unique<Impl>())
	{
		const ma_result result = ma_engine_init(nullptr, &m_Impl->engine);
		m_Impl->ok = result == MA_SUCCESS;
		if (!m_Impl->ok)
		{
			CN_LOG_RUNTIME("Audio", "audio device unavailable (", ma_result_description(result), "); playing silently");
		}
		s_Instance = this;
	}

	AudioSystem::~AudioSystem()
	{
		if (s_Instance == this)
		{
			s_Instance = nullptr;
		}
		if (m_Impl->ok)
		{
			m_Impl->StopAll();
			ma_engine_uninit(&m_Impl->engine);
		}
	}

	AudioSystem* AudioSystem::Get()
	{
		return s_Instance && s_Instance->IsAvailable() ? s_Instance : nullptr;
	}

	bool AudioSystem::IsAvailable() const
	{
		return m_Impl->ok;
	}

	void AudioSystem::Update(Scene* scene, bool playing)
	{
		Impl& impl = *m_Impl;
		if (!impl.ok)
		{
			return;
		}

		if (!playing || !scene)
		{
			if (impl.playing)
			{
				impl.StopAll();
			}
			impl.playing = false;
			return;
		}
		impl.playing = true;

		// 리스너 = 지금 화면을 그리는 카메라
		if (const CameraComponent* camera = scene->GetMainCamera())
		{
			const TransformComponent& transform = *camera->GetTransform();
			const Vector3 position = transform.GetWorldPosition();
			const Vector3 forward = transform.GetForward();
			const Vector3 up = transform.GetUp();
			ma_engine_listener_set_position(&impl.engine, 0, position.X, position.Y, -position.Z);
			ma_engine_listener_set_direction(&impl.engine, 0, forward.X, forward.Y, -forward.Z);
			ma_engine_listener_set_world_up(&impl.engine, 0, up.X, up.Y, -up.Z);
		}

		// 컴포넌트 소스: 시작/정지 처리와 위치 갱신
		for (AudioSourceComponent* source : scene->GetAudioSources())
		{
			if (!source)
			{
				continue;
			}
			if (!Scene::IsRunnable(source))
			{
				StopSource(*source);
				source->MarkAutoStarted(false);
				continue;
			}
			if (source->GetPlayOnStart() && !source->HasAutoStarted())
			{
				source->MarkAutoStarted(true);
				PlaySource(*source);
			}
			if (auto found = impl.sources.find(source); found != impl.sources.end() && source->GetSpatial())
			{
				SetSoundPosition(*found->second, source->GetTransform()->GetWorldPosition());
			}
		}

		// 다 끝난 한 번짜리 소리 정리
		std::erase_if(impl.oneShots, [](std::unique_ptr<ma_sound>& sound)
		{
			if (ma_sound_at_end(sound.get()) || !ma_sound_is_playing(sound.get()))
			{
				ma_sound_uninit(sound.get());
				return true;
			}
			return false;
		});
	}

	void AudioSystem::PlayOneShot(const std::string& clip, const Vector3& position, float volume, float pitch,
		float minDistance, float maxDistance)
	{
		Impl& impl = *m_Impl;
		if (!impl.playing)
		{
			return;
		}
		auto sound = impl.Load(clip, { volume, pitch, false, true, minDistance, maxDistance });
		if (!sound)
		{
			return;
		}
		SetSoundPosition(*sound, position);
		ma_sound_start(sound.get());
		impl.oneShots.push_back(std::move(sound));
	}

	void AudioSystem::Play2D(const std::string& clip, float volume, float pitch)
	{
		Impl& impl = *m_Impl;
		if (!impl.playing)
		{
			return;
		}
		auto sound = impl.Load(clip, { volume, pitch, false, false, 1.0f, 1.0f });
		if (!sound)
		{
			return;
		}
		ma_sound_start(sound.get());
		impl.oneShots.push_back(std::move(sound));
	}

	void AudioSystem::PlaySource(AudioSourceComponent& source)
	{
		Impl& impl = *m_Impl;
		if (!impl.playing)
		{
			return;
		}
		StopSource(source);
		auto sound = impl.Load(source.GetClip(), { source.GetVolume(), source.GetPitch(), source.GetLoop(),
			source.GetSpatial(), source.GetMinDistance(), source.GetMaxDistance() });
		if (!sound)
		{
			return;
		}
		if (source.GetSpatial())
		{
			SetSoundPosition(*sound, source.GetTransform()->GetWorldPosition());
		}
		ma_sound_start(sound.get());
		impl.sources.emplace(&source, std::move(sound));
	}

	void AudioSystem::StopSource(AudioSourceComponent& source)
	{
		ReleaseSource(source);
	}

	bool AudioSystem::IsSourcePlaying(const AudioSourceComponent& source) const
	{
		const auto found = m_Impl->sources.find(&source);
		return found != m_Impl->sources.end() && ma_sound_is_playing(found->second.get());
	}

	void AudioSystem::ReleaseSource(const AudioSourceComponent& source)
	{
		const auto found = m_Impl->sources.find(&source);
		if (found != m_Impl->sources.end())
		{
			ma_sound_uninit(found->second.get());
			m_Impl->sources.erase(found);
		}
	}
}
