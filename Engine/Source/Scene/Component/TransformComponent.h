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

		void SetPosition(const Vector3& position) { m_Position = position; }
		void SetRotation(const Quaternion& rotation) { m_Rotation = rotation; }
		void SetScale(const Vector3& scale) { m_Scale = scale; }

		Vector3 GetWorldPosition() const;
		Quaternion GetWorldRotation() const;
		// The product of every local scale up the chain; rotation between the scales is ignored.
		Vector3 GetLossyScale() const;

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

		Vector3 m_Position = Vector3::Zero;
		Quaternion m_Rotation = Quaternion::Identity;
		Vector3 m_Scale = Vector3::One;

		TransformComponent* m_Parent = nullptr;
		std::vector<TransformComponent*> m_Children;
	};
}
