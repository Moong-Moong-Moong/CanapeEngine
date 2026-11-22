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
			child->MarkWorldDirty();
		}
	}

	void TransformComponent::SetPosition(const Vector3& position)
	{
		if (m_Position == position)
		{
			return;
		}
		m_Position = position;
		MarkWorldDirty();
	}

	void TransformComponent::SetRotation(const Quaternion& rotation)
	{
		if (m_Rotation == rotation)
		{
			return;
		}
		m_Rotation = rotation;
		MarkWorldDirty();
	}

	void TransformComponent::SetScale(const Vector3& scale)
	{
		if (m_Scale == scale)
		{
			return;
		}
		m_Scale = scale;
		MarkWorldDirty();
	}

	void TransformComponent::MarkWorldDirty()
	{
		if (m_WorldDirty)
		{
			return;
		}
		m_WorldDirty = true;
		m_InverseDirty = true;
		for (TransformComponent* child : m_Children)
		{
			child->MarkWorldDirty();
		}
	}

	void TransformComponent::UpdateWorld() const
	{
		if (!m_WorldDirty)
		{
			return;
		}

		const Matrix4 local = GetLocalMatrix();
		if (m_Parent)
		{
			m_Parent->UpdateWorld();
			m_LocalToWorld = local * m_Parent->m_LocalToWorld;
			m_WorldRotation = (m_Parent->m_WorldRotation * m_Rotation).Normalized();
			m_LossyScale = m_Parent->m_LossyScale * m_Scale;
		}
		else
		{
			m_LocalToWorld = local;
			m_WorldRotation = m_Rotation;
			m_LossyScale = m_Scale;
		}
		m_WorldDirty = false;
		m_InverseDirty = true;
	}

	Vector3 TransformComponent::GetWorldPosition() const
	{
		if (!m_Parent)
		{
			return m_Position;
		}
		UpdateWorld();
		return m_LocalToWorld.GetTranslation();
	}

	Quaternion TransformComponent::GetWorldRotation() const
	{
		UpdateWorld();
		return m_WorldRotation;
	}

	Vector3 TransformComponent::GetLossyScale() const
	{
		UpdateWorld();
		return m_LossyScale;
	}

	Vector3 TransformComponent::GetRight() const
	{
		return GetWorldRotation().GetRight();
	}

	Vector3 TransformComponent::GetUp() const
	{
		return GetWorldRotation().GetUp();
	}

	Vector3 TransformComponent::GetForward() const
	{
		return GetWorldRotation().GetForward();
	}

	void TransformComponent::LookAt(const Vector3& target, const Vector3& up)
	{
		const Vector3 direction = target - GetWorldPosition();
		if (direction.IsNearlyZero())
		{
			return;
		}

		const Quaternion worldRotation = Quaternion::LookRotation(direction, up);
		SetRotation(m_Parent ? (m_Parent->GetWorldRotation().Inverse() * worldRotation).Normalized() : worldRotation);
	}

	Matrix4 TransformComponent::GetLocalMatrix() const
	{
		return Matrix4::TRS(m_Position, m_Rotation, m_Scale);
	}

	Matrix4 TransformComponent::GetLocalToWorldMatrix() const
	{
		UpdateWorld();
		return m_LocalToWorld;
	}

	Matrix4 TransformComponent::GetWorldToLocalMatrix() const
	{
		UpdateWorld();
		if (m_InverseDirty)
		{
			m_WorldToLocal = m_LocalToWorld.Inverse();
			m_InverseDirty = false;
		}
		return m_WorldToLocal;
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

			if (parent->GetOwnerObject()->GetScene() != GetOwnerObject()->GetScene())
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

		MarkWorldDirty();
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
