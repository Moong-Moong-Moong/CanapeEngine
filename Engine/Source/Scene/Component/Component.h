#pragma once

#include <rttr/rttr_enable.h>

#include <cstdint>

namespace Canape
{
	class GameObject;
	class TransformComponent;

	class Component
	{
		RTTR_ENABLE()

	public:
		virtual ~Component() = default;

		Component(const Component&) = delete;
		Component& operator=(const Component&) = delete;

		GameObject* GetOwnerObject() const { return m_GameObject; }
		TransformComponent* GetTransform() const;

		uint64_t GetId() const { return m_Id; }
		void SetId(uint64_t id) { m_Id = id; }

		bool IsEnabled() const { return m_Enabled; }
		void SetEnabled(bool enabled) { m_Enabled = enabled; }
		bool IsPendingDestroy() const { return m_PendingDestroy; }

	protected:
		Component() = default;

	private:
		friend class GameObject;

		GameObject* m_GameObject = nullptr;
		uint64_t m_Id = 0;
		bool m_Enabled = true;
		bool m_PendingDestroy = false;
	};
}
