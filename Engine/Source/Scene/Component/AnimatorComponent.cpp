#include "Scene/Component/AnimatorComponent.h"
#include "Asset/Animation/AnimationClipAsset.h"
#include "Core/Application.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"

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

		// root 기준 "A/B/C" 경로를 자식 이름으로 따라간다. 빈 경로는 root 자신.
		TransformComponent* FindByPath(TransformComponent* root, const std::string& path)
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

		Vector3 ReadVector3(const std::vector<float>& values, uint32_t key)
		{
			const size_t i = static_cast<size_t>(key) * 3;
			return { values[i], values[i + 1], values[i + 2] };
		}

		Quaternion ReadQuaternion(const std::vector<float>& values, uint32_t key)
		{
			const size_t i = static_cast<size_t>(key) * 4;
			return { values[i], values[i + 1], values[i + 2], values[i + 3] };
		}

		Quaternion Slerp(const Quaternion& a, const Quaternion& b, float s)
		{
			// XMQuaternionSlerp는 dot < 0이면 짧은 경로로 뒤집어서 보간한다.
			return Quaternion::FromXM(DirectX::XMQuaternionSlerp(a.ToXM(), b.ToXM(), s)).Normalized();
		}
	}

	bool AnimatorComponent::SetClip(const std::string& clipPath)
	{
		m_ClipPath = clipPath;
		m_Time = 0.0f;
		m_Targets.clear();
		m_LastKeys.clear();

		GameObject* gameObject = GetOwnerObject();
		if (!gameObject)
		{
			m_Clip.Reset();
			return false;
		}

		AssetManager& assets = Application::Get().GetAssetManager();
		m_Clip = assets.GetAnimationClipHandle(clipPath, MakeOwnerInfo(*gameObject));
		if (const AnimationClipAsset* clip = assets.GetAnimationClip(m_Clip))
		{
			Bind(*clip);
			return true;
		}
		return false;
	}

	void AnimatorComponent::Bind(const AnimationClipAsset& clip)
	{
		const std::vector<AnimationTrack>& tracks = clip.GetTracks();
		m_Targets.assign(tracks.size(), nullptr);
		m_LastKeys.assign(tracks.size(), 0);

		TransformComponent* root = GetTransform();
		for (size_t i = 0; i < tracks.size(); ++i)
		{
			m_Targets[i] = FindByPath(root, tracks[i].targetPath);
		}
	}

	void AnimatorComponent::Evaluate(float deltaTime)
	{
		const AnimationClipAsset* clip = Application::Get().GetAssetManager().GetAnimationClip(m_Clip);
		if (!clip)
		{
			return;
		}

		const std::vector<AnimationTrack>& tracks = clip->GetTracks();
		if (m_Targets.size() != tracks.size())
		{
			Bind(*clip);
		}

		// 1. 시간 진행
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
			TransformComponent* target = m_Targets[i];
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
				target->SetLocalPosition(Lerp(ReadVector3(track.values, key), ReadVector3(track.values, next), s));
				break;
			case AnimationProperty::Rotation:
				target->SetLocalRotation(Slerp(ReadQuaternion(track.values, key), ReadQuaternion(track.values, next), s));
				break;
			case AnimationProperty::Scale:
				target->SetLocalScale(Lerp(ReadVector3(track.values, key), ReadVector3(track.values, next), s));
				break;
			}
		}
	}
}
