#pragma once

#include "Scene/Component/Component.h"
#include "Math/Matrix4.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"

#include <cstddef>
#include <vector>

namespace Canape
{
	class TransformComponent final : public Component
	{
		RTTR_ENABLE(Component)

	public:
		~TransformComponent() override;

		const Vector3& GetPosition() const { return m_Position; }
		const Quaternion& GetRotation() const { return m_Rotation; }
		const Vector3& GetScale() const { return m_Scale; }

		// 값이 실제로 바뀔 때만 자신과 자식들의 월드 캐시를 무효화한다.
		void SetPosition(const Vector3& position);
		void SetRotation(const Quaternion& rotation);
		void SetScale(const Vector3& scale);

		Vector3 GetWorldPosition() const;
		Quaternion GetWorldRotation() const;
		// The product of every local scale up the chain; rotation between the scales is ignored.
		Vector3 GetLossyScale() const;

		Vector3 GetRight() const;
		Vector3 GetUp() const;
		Vector3 GetForward() const;

		void LookAt(const Vector3& target, const Vector3& up = Vector3::Up);

		Matrix4 GetLocalMatrix() const;
		// 월드 값들은 캐시된다. 자신이나 조상의 로컬 값/부모가 바뀌기 전까지 다시 계산하지 않는다.
		Matrix4 GetLocalToWorldMatrix() const;
		Matrix4 GetWorldToLocalMatrix() const;

		TransformComponent* GetParent() const { return m_Parent; }
		void SetParent(TransformComponent* parent);
		bool IsChildOf(const TransformComponent* ancestor) const;

		const std::vector<TransformComponent*>& GetChildren() const { return m_Children; }
		std::size_t GetChildCount() const { return m_Children.size(); }
		TransformComponent* GetChild(std::size_t index) const { return index < m_Children.size() ? m_Children[index] : nullptr; }

	private:
		friend class GameObject;

		TransformComponent() = default;

		// 자신과 모든 자손의 월드 캐시를 무효화한다.
		// 불변식: dirty인 노드의 자손은 모두 dirty다. 그래서 이미 dirty면 내려가지 않고 바로 끝낸다.
		void MarkWorldDirty();
		// dirty일 때만 부모 캐시를 이용해 월드 값을 다시 계산한다.
		void UpdateWorld() const;

		Vector3 m_Position = Vector3::Zero;
		Quaternion m_Rotation = Quaternion::Identity;
		Vector3 m_Scale = Vector3::One;

		TransformComponent* m_Parent = nullptr;
		std::vector<TransformComponent*> m_Children;

		// 월드 캐시 (const getter에서 지연 계산)
		mutable Matrix4 m_LocalToWorld{};
		mutable Matrix4 m_WorldToLocal{};
		mutable Quaternion m_WorldRotation = Quaternion::Identity;
		mutable Vector3 m_LossyScale = Vector3::One;
		mutable bool m_WorldDirty = true;
		mutable bool m_InverseDirty = true;
	};
}
