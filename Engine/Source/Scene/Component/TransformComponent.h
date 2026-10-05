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
	public:
		~TransformComponent() override;

		const Vector3& GetLocalPosition() const { return m_LocalPosition; }
		const Quaternion& GetLocalRotation() const { return m_LocalRotation; }
		const Vector3& GetLocalScale() const { return m_LocalScale; }

		void SetLocalPosition(const Vector3& position) { m_LocalPosition = position; }
		void SetLocalRotation(const Quaternion& rotation) { m_LocalRotation = rotation; }
		void SetLocalScale(const Vector3& scale) { m_LocalScale = scale; }

		Vector3 GetPosition() const;
		Quaternion GetRotation() const;

		Vector3 GetRight() const;
		Vector3 GetUp() const;
		Vector3 GetForward() const;

		void LookAt(const Vector3& target, const Vector3& up = Vector3::Up);

		Matrix4 GetLocalMatrix() const;
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

		Vector3 m_LocalPosition = Vector3::Zero;
		Quaternion m_LocalRotation = Quaternion::Identity;
		Vector3 m_LocalScale = Vector3::One;

		TransformComponent* m_Parent = nullptr;
		std::vector<TransformComponent*> m_Children;
	};
}
