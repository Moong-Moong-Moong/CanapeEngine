#include "Scene/Component/TransformComponent.h"
#include "Scene/GameObject/GameObject.h"

#include <algorithm>

namespace Canape
{
	TransformComponent::~TransformComponent()
	{
		if (m_Parent)
		{
			std::erase(m_Parent->m_Children, this);
		}

		for (TransformComponent* child : m_Children)
		{
			child->m_Parent = nullptr;
		}
	}

	Vector3 TransformComponent::GetPosition() const
	{
		return m_Parent ? m_Parent->GetLocalToWorldMatrix().TransformPoint(m_LocalPosition) : m_LocalPosition;
	}

	Quaternion TransformComponent::GetRotation() const
	{
		return m_Parent ? (m_Parent->GetRotation() * m_LocalRotation).Normalized() : m_LocalRotation;
	}

	Vector3 TransformComponent::GetRight() const
	{
		return GetRotation().GetRight();
	}

	Vector3 TransformComponent::GetUp() const
	{
		return GetRotation().GetUp();
	}

	Vector3 TransformComponent::GetForward() const
	{
		return GetRotation().GetForward();
	}

	void TransformComponent::LookAt(const Vector3& target, const Vector3& up)
	{
		const Vector3 direction = target - GetPosition();
		if (direction.IsNearlyZero())
		{
			return;
		}

		const Quaternion worldRotation = Quaternion::LookRotation(direction, up);
		m_LocalRotation = m_Parent ? (m_Parent->GetRotation().Inverse() * worldRotation).Normalized() : worldRotation;
	}

	Matrix4 TransformComponent::GetLocalMatrix() const
	{
		return Matrix4::TRS(m_LocalPosition, m_LocalRotation, m_LocalScale);
	}

	Matrix4 TransformComponent::GetLocalToWorldMatrix() const
	{
		const Matrix4 local = GetLocalMatrix();
		return m_Parent ? local * m_Parent->GetLocalToWorldMatrix() : local;
	}

	Matrix4 TransformComponent::GetWorldToLocalMatrix() const
	{
		return GetLocalToWorldMatrix().Inverse();
	}

	void TransformComponent::SetParent(TransformComponent* parent)
	{
		if (parent == m_Parent || parent == this)
		{
			return;
		}

		if (parent)
		{
			if (parent->IsChildOf(this))
			{
				return;
			}

			if (parent->GetGameObject()->GetScene() != GetGameObject()->GetScene())
			{
				return;
			}
		}

		if (m_Parent)
		{
			std::erase(m_Parent->m_Children, this);
		}

		m_Parent = parent;

		if (m_Parent)
		{
			m_Parent->m_Children.push_back(this);
		}

		GetGameObject()->RefreshHierarchy();
	}

	bool TransformComponent::IsChildOf(const TransformComponent* ancestor) const
	{
		for (const TransformComponent* current = m_Parent; current; current = current->m_Parent)
		{
			if (current == ancestor)
			{
				return true;
			}
		}
		return false;
	}
}
