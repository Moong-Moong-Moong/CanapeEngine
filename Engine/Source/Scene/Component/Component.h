#pragma once

namespace Canape
{
	class GameObject;
	class TransformComponent;

	class Component
	{
	public:
		virtual ~Component() = default;

		Component(const Component&) = delete;
		Component& operator=(const Component&) = delete;

		GameObject* GetGameObject() const { return m_GameObject; }
		TransformComponent* GetTransform() const;

		bool IsEnabled() const { return m_Enabled; }
		void SetEnabled(bool enabled);
		bool IsActiveAndEnabled() const;
		bool IsPendingDestroy() const { return m_PendingDestroy; }

	protected:
		Component() = default;

	private:
		friend class GameObject;

		GameObject* m_GameObject = nullptr;
		bool m_Enabled = true;
		bool m_PendingDestroy = false;
	};
}
