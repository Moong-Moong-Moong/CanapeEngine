#include "Scene/Component/AnimatorComponent.h"
#include "Asset/Animation/AnimationClipAsset.h"
#include "Core/Application.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace Canape
{
	namespace
	{
		AssetOwnerInfo MakeOwnerInfo(const GameObject& gameObject)
		{
			AssetOwnerInfo owner;
			owner.sceneName = gameObject.GetScene() ? gameObject.GetScene()->GetName() : std::string();
			owner.objectName = gameObject.GetName();
			owner.componentName = "AnimatorComponent";
			return owner;
		}

		TransformComponent* FindByPath(TransformComponent* root, const std::string& path) // path : Armature/mixamorig.Hips/mixamorig.Spine 
		{
			TransformComponent* current = root;
			size_t begin = 0;
			while (current && begin < path.size())
			{
				size_t end = path.find('/', begin);
				if (end == std::string::npos)
				{
					end = path.size();
				}
				const std::string_view name(path.data() + begin, end - begin);

				TransformComponent* next = nullptr;
				for (TransformComponent* child : current->GetChildren())
				{
					const GameObject* object = child ? child->GetOwnerObject() : nullptr;
					if (object && object->GetName() == name)
					{
						next = child;
						break;
					}
				}
				current = next;
				begin = end + 1;
			}
			return current;
		}

		// times[key] <= time < times[key + 1] 인 key를 찾는다. 이전 프레임 key에서 앞으로만 진행한다.

		uint32_t FindKey(const std::vector<float>& times, float time, uint32_t lastKey) // 키, 현재시간, 이전 키
		{
			const uint32_t keyCount = static_cast<uint32_t>(times.size());
			uint32_t key = lastKey < keyCount &&  (lastKey < keyCount && times[lastKey] <= time) ? lastKey : 0;
			while (key + 1 < keyCount && times[key + 1] <= time)
			{
				++key;
			}
			return key;
		}

		Vector3 ReadVector3(const std::vector<Vector4>& values, uint32_t key)
		{
			const Vector4& v = values[key];
			return { v.X, v.Y, v.Z };
		}

		Quaternion ReadQuaternion(const std::vector<Vector4>& values, uint32_t key)
		{
			const Vector4& v = values[key];
			return { v.X, v.Y, v.Z, v.W };
		}

		Quaternion Slerp(const Quaternion& a, const Quaternion& b, float s)
		{
			// XMQuaternionSlerp는 dot < 0이면 짧은 경로로 뒤집어서 보간한다.
			return Quaternion::FromXM(DirectX::XMQuaternionSlerp(a.ToXM(), b.ToXM(), s)).Normalized();
		}
	}

	AnimatorComponent::ClipBinding* AnimatorComponent::FindOrLoad(const std::string& clipPath)
	{
		auto found = m_Bindings.find(clipPath);
		if (found != m_Bindings.end() && found->second.clip.IsValid())
		{
			return &found->second;
		}

		GameObject* gameObject = GetOwnerObject();
		if (!gameObject || clipPath.empty())
		{
			return nullptr;
		}

		AssetManager& assets = Application::Get().GetAssetManager();
		ClipBinding& binding = m_Bindings[clipPath];
		binding.clip = assets.GetAnimationClipHandle(clipPath, MakeOwnerInfo(*gameObject));
		binding.targets.clear();
		return assets.GetAnimationClip(binding.clip) ? &binding : nullptr;
	}

	bool AnimatorComponent::SetClip(const std::string& clipPath)
	{
		if (m_ClipPath == clipPath) return true;

		m_ClipPath = clipPath;
		m_Time = 0.0f;
		m_LastKeys.clear();
		// 미리 로드해 둔 클립이면 로드도 바인딩도 하지 않는다.
		m_Current = FindOrLoad(clipPath);
		return m_Current != nullptr;
	}

	void AnimatorComponent::PreloadClips()
	{
		m_Preloaded = true;
		AssetManager& assets = Application::Get().GetAssetManager();
		auto prepare = [&](const std::string& path)
		{
			ClipBinding* binding = FindOrLoad(path);
			const AnimationClipAsset* clip = binding ? assets.GetAnimationClip(binding->clip) : nullptr;
			if (clip && binding->targets.size() != clip->GetTracks().size())
			{
				Bind(*clip, binding->targets);
			}
		};
		for (const std::string& path : m_PreloadClips)
		{
			prepare(path);
		}
		prepare(m_ClipPath);
		// SetClip이 계층이 다 만들어지기 전(역직렬화 중)에 불렸을 수 있으니 현재 클립 포인터를 다시 잡는다.
		m_Current = FindOrLoad(m_ClipPath);
	}

	float AnimatorComponent::GetDuration() const
	{
		const AnimationClipAsset* clip = m_Current ? Application::Get().GetAssetManager().GetAnimationClip(m_Current->clip) : nullptr;
		return clip ? clip->GetDuration() : 0.0f;
	}

	void AnimatorComponent::Bind(const AnimationClipAsset& clip, std::vector<TrackTarget>& targets) // track
	{
		const std::vector<AnimationTrack>& tracks = clip.GetTracks();
		targets.assign(tracks.size(), TrackTarget{});

		TransformComponent* root = GetTransform();
		for (size_t i = 0; i < tracks.size(); ++i)
		{
			const std::string& path = tracks[i].targetPath;
			targets[i].transform = FindByPath(root, path);
			targets[i].isTranslationRoot =
				tracks[i].property == AnimationProperty::Translation && std::count(path.begin(), path.end(), '/') == 1;
		}
	}

	void AnimatorComponent::ApplyKeyFrame(float deltaTime)
	{
		if (!m_Preloaded)
		{
			PreloadClips();
		}
		const AnimationClipAsset* clip = m_Current ? Application::Get().GetAssetManager().GetAnimationClip(m_Current->clip) : nullptr;
		if (!clip)
		{
			return;
		}

		const std::vector<AnimationTrack>& tracks = clip->GetTracks();
		std::vector<TrackTarget>& targets = m_Current->targets;
		if (targets.size() != tracks.size())
		{
			Bind(*clip, targets);
		}
		if (m_LastKeys.size() != tracks.size())
		{
			m_LastKeys.assign(tracks.size(), 0);
		}

		const float duration = clip->GetDuration();
		if (m_Playing)
		{
			m_Time += deltaTime * m_Speed;
			if (duration > 0.0f)
			{
				if (m_Loop)
				{
					m_Time = std::fmod(m_Time, duration); // m_speed가 음수가되면 역재생 되도록함
					if (m_Time < 0.0f)
					{
						m_Time += duration;
					}
				}
				else if (m_Time >= duration)
				{
					m_Time = duration;
					m_Playing = false;
				}
				else if (m_Time < 0.0f)
				{
					m_Time = 0.0f;
					m_Playing = false;
				}
			}
		}

		// 2. 트랙별 샘플링 → 대상 Transform에 기록
		for (size_t i = 0; i < tracks.size(); ++i)
		{
			const AnimationTrack& track = tracks[i];
			TransformComponent* target = targets[i].transform;
			const uint32_t keyCount = track.GetKeyCount();
			if (!target || keyCount == 0) // 타겟 본이 없거나 키 카운트가 0이면
			{
				continue;
			}

			// 첫 키 이전/마지막 키 이후는 그 키 값으로 고정된다.
			const uint32_t key = FindKey(track.times, m_Time, m_LastKeys[i]);
			m_LastKeys[i] = key;

			uint32_t next = key;
			float s = 0.0f;
			if (track.interpolation == AnimationInterpolation::Linear && key + 1 < keyCount && m_Time > track.times[key])
			{
				next = key + 1;
				const float span = track.times[next] - track.times[key];
				s = span > 0.0f ? (m_Time - track.times[key]) / span : 0.0f;
			}

			switch (track.property)
			{
			case AnimationProperty::Translation:
			{
				Vector3 position = Lerp(ReadVector3(track.values, key), ReadVector3(track.values, next), s);
				if (!m_rootMotion && targets[i].isTranslationRoot)
				{
					const TransformComponent* parent = target->GetParent();
					const Vector3 up = parent ? parent->GetWorldRotation().Inverse().Rotate(Vector3::Up).Normalized() : Vector3::Up;
					const Vector3 anchor = ReadVector3(track.values, 0);
					position = anchor + up * Vector3::Dot(position - anchor, up);
				}
				target->SetPosition(position);
				break;
			}
			case AnimationProperty::Rotation:
				target->SetRotation(Slerp(ReadQuaternion(track.values, key), ReadQuaternion(track.values, next), s));
				break;
			case AnimationProperty::Scale:
				target->SetScale(Lerp(ReadVector3(track.values, key), ReadVector3(track.values, next), s));
				break;
			}
		}
	}
}
